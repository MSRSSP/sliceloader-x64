#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

#include "utils.h"
#include "linuxboot.h"
#include "mptable.h"
#include "runslice.h"

extern "C" char realmode_blob_start[];
extern "C" size_t realmode_blob_size;

struct realmode_header {
    uint64_t reserved;
    uint64_t kernel_entry;
    uint64_t kernel_arg;
} __attribute__((__packed__));

[[noreturn]] static void usage(const char* errmsg = nullptr)
{
    if (errmsg)
        std::cerr << "Error: " << errmsg << std::endl;

    std::cerr << "Usage: runslice [OPTIONS]" << std::endl
        << "  -kernel PATH    Kernel image to boot. Required." << std::endl
        << "  -initrd PATH    RAM disk image." << std::endl
        << "  -cmdline CMD    Kernel command line." << std::endl
        << "  -rambase ADDR   Physical base address of slice memory." << std::endl
        << "  -ramsize SIZE   Size of slice memory." << std::endl
        << "  -cpus APICIDS   Comma-separated list of APIC IDs." << std::endl;

    exit(1);
}

struct Options
{
    const char* kernel_path = nullptr;
    const char* initrd_path = nullptr;
    const char* kernel_cmdline = nullptr;
    uint64_t rambase = 0;
    uint64_t ramsize = 0;
    std::vector<uint32_t> apic_ids;

    void validate()
    {
        if (kernel_path == nullptr)
            usage("Kernel image path is required");
        if (rambase == 0 || ramsize == 0)
            usage("RAM base and size are required");
        if (rambase % 0x1000 != 0)
            usage("RAM base must be page-aligned");
        if (ramsize % 0x1000 != 0)
            usage("RAM size must be page-aligned");
        if (apic_ids.empty())
            usage("APIC IDs are required");
    }
};

static void parse_cpus(const char* str, std::vector<uint32_t>& apic_ids)
{
    apic_ids.clear();

    char* end;
    for (const char* p = str; *p; p++) {
        if (*p == ',') {
            apic_ids.push_back(strtoul(str, &end, 0));
            assert(end == p);
            str = end + 1;
        }
    }

    apic_ids.push_back(strtoul(str, &end, 0));
}

static void parse_args(int argc, const char* argv[], Options& options)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-kernel") == 0) {
            if (++i >= argc)
                usage();
            options.kernel_path = argv[i];
        } else if (strcmp(argv[i], "-initrd") == 0) {
            if (++i >= argc)
                usage();
            options.initrd_path = argv[i];
        } else if (strcmp(argv[i], "-cmdline") == 0) {
            if (++i >= argc)
                usage();
            options.kernel_cmdline = argv[i];
        } else if (strcmp(argv[i], "-rambase") == 0) {
            if (++i >= argc)
                usage();
            options.rambase = strtoul(argv[i], nullptr, 0);
        } else if (strcmp(argv[i], "-ramsize") == 0) {
            if (++i >= argc)
                usage();
            options.ramsize = strtoul(argv[i], nullptr, 0);
        } else if (strcmp(argv[i], "-cpus") == 0) {
            if (++i >= argc)
                usage();
            parse_cpus(argv[i], options.apic_ids);
        } else {
            usage();
        }
    }
}

static void fill_e820_table(const Options& options, boot_params& params)
{
    constexpr int E820_RAM = 1;

    // XXX: In order to boot secondary CPUs, Linux needs a small region of real-mode (<1MiB PA)
    // memory, allocated on boot by reserve_real_mode(). We also happen to know that after boot,
    // Linux unconditionally reserves (and thus avoids touching) the first 1MiB of memory, so we
    // should be safe to use it here.
    params.e820_table[0] = { .addr = 0, .size = 639 * 1024, .type = E820_RAM };
    params.e820_table[1] = { .addr = options.rambase, .size = options.ramsize, .type = E820_RAM };
    params.e820_entries = 2;
    static_assert(2 <= E820_MAX_ENTRIES_ZEROPAGE);
}

static bool read_to_devmem(std::ifstream& file, uint64_t offset, void* dest, size_t size)
{
    // Linux doesn't permit I/O directly to a mapping of /dev/mem, so we must use a temporary
    // buffer.
    if (!file.seekg(offset, std::ios::beg))
        return false;

    while (size > 0) {
        char buf[0x8000];
        const size_t chunk = std::min(size, sizeof(buf));

        if (!file.read(buf, chunk))
            return false;

        memcpy(dest, buf, chunk);
        dest = static_cast<char*>(dest) + chunk;
        size -= chunk;
    }

    return true;
}

static bool load_linux(
    const Options& options,
    void* slice_ram,
    uintptr_t& kernel_entry_phys, // Out: entry point at which to jump to kernel in 64-bit mode
    uintptr_t& kernel_entry_arg)  // Out: argument to be passed to kernel entry (in RSI)
{
    static constexpr size_t header_offset = offsetof(boot_params, hdr);
    static_assert(header_offset == 0x1f1);
    setup_header header;

    // open the kernel image, and determine its size
    std::ifstream kernel_file(options.kernel_path, std::ios::binary | std::ios::in);
    if (!kernel_file.is_open()) {
        perror("Failed to open kernel image");
        return false;
    }

    kernel_file.seekg(0, std::ios::end);
    size_t kernel_file_size = kernel_file.tellg();
    if (kernel_file_size < header_offset + sizeof(header))
    {
        std::cerr << "Invalid kernel image: file is far too small" << std::endl;
        return false;
    }

    // Read and check the setup header.
    if (!kernel_file.seekg(header_offset)
        || !kernel_file.read(reinterpret_cast<char*>(&header), sizeof(header))) {
        perror("Failed to read kernel image header");
        return false;
    }

    if (header.header != 0x53726448 || header.version < 0x20c || header.setup_sects == 0) {
        std::cerr << "Invalid or too old kernel image" << std::endl;
        return false;
    }

    if (!header.relocatable_kernel ||
        !(header.xloadflags & (XLF_KERNEL_64 | XLF_CAN_BE_LOADED_ABOVE_4G))) {
        std::cerr << "Kernel image is not relocatable" << std::endl;
        return false;
    }

    // Sanity-check size of file.
    size_t kernel_image_offset = 512 * ((header.setup_sects ? header.setup_sects : 4) + 1);
    if (kernel_image_offset >= kernel_file_size) {
        std::cerr << "Invalid kernel image (file has been truncated)" << std::endl;
        return false;
    }

	// Load the kernel first.
    uintptr_t loadaddr_phys = ALIGN_UP(options.rambase, header.kernel_alignment);
    printf("Loading Linux at 0x%lx\n", loadaddr_phys);
    char* loadaddr_virt = reinterpret_cast<char*>(slice_ram) + (loadaddr_phys - options.rambase);

    if (!read_to_devmem(kernel_file, kernel_image_offset, loadaddr_virt, kernel_file_size - kernel_image_offset)) {
        perror("Failed to read kernel image");
        return false;
    }

    kernel_entry_phys = loadaddr_phys + 0x200;

    // Leave space required for early boot code.
    {
        size_t header_size = ALIGN_UP(header.init_size, 0x1000);
        loadaddr_phys += header_size;
        loadaddr_virt += header_size;
    }

	// Boot params ("zero page") follows the kernel.
	struct boot_params *boot_params = reinterpret_cast<struct boot_params*>(loadaddr_virt);
    kernel_entry_arg = loadaddr_phys;

	memset(boot_params, 0, sizeof(*boot_params));
	boot_params->hdr = header;

	loadaddr_phys += sizeof(*boot_params);
    loadaddr_virt += sizeof(*boot_params);

	// ACPI tables.
	boot_params->acpi_rsdp_addr =
        build_acpi(loadaddr_phys, loadaddr_virt, options.rambase, options.ramsize, options.apic_ids);

	// Command line.
    if (options.kernel_cmdline) {
        strcpy(loadaddr_virt, options.kernel_cmdline);
        boot_params->hdr.cmd_line_ptr = static_cast<uint32_t>(loadaddr_phys);
        boot_params->ext_cmd_line_ptr = loadaddr_phys >> 32;
        size_t cmdline_size = ALIGN_UP(strlen(options.kernel_cmdline) + 1, 8);
	    loadaddr_phys += cmdline_size;
        loadaddr_virt += cmdline_size;
    }

	// Load initrd if present.
    if (options.initrd_path)
    {
        std::ifstream initrd_file(options.initrd_path, std::ios::binary | std::ios::in);
        if (!initrd_file.is_open()) {
            perror("Failed to open initrd");
            return false;
        }

        initrd_file.seekg(0, std::ios::end);
        size_t initrd_size = initrd_file.tellg();

        if (!read_to_devmem(initrd_file, 0, loadaddr_virt, initrd_size)) {
            perror("Failed to read initrd");
            return false;
        }

        boot_params->hdr.ramdisk_size = initrd_size;

        boot_params->hdr.ramdisk_image = static_cast<uint32_t>(loadaddr_phys);
        boot_params->ext_ramdisk_image = loadaddr_phys >> 32;
    }

	boot_params->hdr.type_of_loader = 0xff;

    fill_e820_table(options, *boot_params);

    return true;
}

static uintptr_t obliterate_mptable_range(void* lowmem, uintptr_t base, uintptr_t size)
{
    constexpr uint32_t BOGUS_MAGIC = ('-'<<24) | ('P'<<16) | ('M'<<8) | '-';

    assert(base % sizeof(uint32_t) == 0);
    assert(size % sizeof(uint32_t) == 0);

    uint32_t* const start = reinterpret_cast<uint32_t*>(static_cast<char*>(lowmem) + base);
    uint32_t* const end = start + size / sizeof(uint32_t);

    uintptr_t found = 0;

    for (uint32_t* p = start; p < end; p++)
    {
        if (*p == MPTABLE_PTR_MAGIC)
        {
            const uintptr_t addr =  base + (p - start) * sizeof(*p);
            if (!found)
                found = addr;

            printf("Obliterating MP table at 0x%lx\n", addr);
            *p = BOGUS_MAGIC;
        }
    }

    return found;
}

static void write_mptable(const Options& options, void* vaddr, uintptr_t paddr)
{
    MPFloatingPointer* mpfp = static_cast<MPFloatingPointer*>(vaddr);
    memset(mpfp, 0, sizeof(*mpfp));
    mpfp->signature = MPTABLE_PTR_MAGIC;
    mpfp->length = 1;
    mpfp->specrev = 4;
    mpfp->physaddr = paddr + sizeof(*mpfp);
    mpfp->checksum = acpi_checksum(mpfp, sizeof(*mpfp));

    MPConfigHeader* mpt = reinterpret_cast<MPConfigHeader*>(static_cast<char*>(vaddr) + sizeof(*mpfp));
    memset(mpt, 0, sizeof(*mpt));
    mpt->signature = MPTABLE_CONFIG_MAGIC;
    mpt->base_length = sizeof(*mpt) + options.apic_ids.size() * sizeof(MPProcessorEntry);
    mpt->spec_rev = 4;
    memset(mpt->oemid, ' ', sizeof(mpt->oemid));
    memset(mpt->prodid, ' ', sizeof(mpt->oemid));
    memcpy(mpt->oemid, "SLICER", 6);
    memcpy(mpt->prodid, "SLICER", 6);
    mpt->entries = options.apic_ids.size();
    mpt->lapic_addr = 0xfee00000;

    MPProcessorEntry* mpp = reinterpret_cast<MPProcessorEntry*>(mpt + 1);


    // Assume uniform CPUs.
    uint16_t family_model_stepping;
    uint32_t cpu_feature_flags;
    {
        uint32_t eax, ebx, ecx;
        cpuid(1, 0, eax, ebx, ecx, cpu_feature_flags);
        family_model_stepping = eax & 0xfff;
    }

    bool first = true;
    for (uint32_t apic_id : options.apic_ids)
    {
        memset(mpp, 0, sizeof(*mpp));
        mpp->type = MPEntryType::Processor;
        mpp->apic_id = apic_id;
        mpp->apic_ver = 0x14;
        mpp->cpu_flags = MP_PROCESSOR_ENABLED;
        if (first) {
            mpp->cpu_flags |= MP_PROCESSOR_BSP;
            first = false;
        }

        mpp->cpu_signature = family_model_stepping;
        mpp->feature_flags = cpu_feature_flags;
    }

    mpt->checksum = acpi_checksum(mpt, mpt->base_length);
}

static bool lowmem_init(const Options& options, const AutoFd& devmem, uintptr_t kernel_entry, uintptr_t kernel_arg, uintptr_t &boot_ip)
{
    constexpr size_t KiB = 0x400;
    constexpr size_t MiB = 0x100000;

    void* lowmem = mmap(nullptr, MiB, PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 0);
    if (lowmem == MAP_FAILED) {
        perror("Error: Failed to map first MiB of RAM");
        return false;
    }

    // Address of physical memory to steal for our realmode blob.
    // TODO: try to allocate this more safely? It can be anywhere in low memory.
    constexpr uintptr_t REALMODE_BOOT_ADDR = 0x6000;

    struct realmode_header* realmode_header = reinterpret_cast<struct realmode_header*>(realmode_blob_start);
    assert(realmode_blob_size > sizeof(*realmode_header));
    assert(realmode_header->kernel_entry == 0x5c3921544fd4ae2d);
    realmode_header->kernel_entry = kernel_entry;
    realmode_header->kernel_arg = kernel_arg;

    memcpy(static_cast<char*>(lowmem) + REALMODE_BOOT_ADDR, realmode_blob_start, realmode_blob_size);

    // Scan through the same memory ranges that Linux does looking for MPTABLE structures.
    uintptr_t mptable1_pa = obliterate_mptable_range(lowmem, 0, KiB); // bottom 1K
    uintptr_t mptable2_pa = obliterate_mptable_range(lowmem, 639 * KiB, KiB); // top 1K of base RAM

    // This won't work for any MPTABLE structures in the BIOS area, so instead we need to write
    // our own in low-memory. We try to overwrite an existing one, but fall back to punting on an
    // arbitrary address.
    constexpr uintptr_t FALLBACK_MPTABLE_ADDR = 639 * KiB;
    uintptr_t mptable_pa = mptable1_pa ? mptable1_pa : (mptable2_pa ? mptable2_pa : FALLBACK_MPTABLE_ADDR);
    printf("Dummy MP table at 0x%lx\n", mptable_pa);
    write_mptable(options, static_cast<char*>(lowmem) + mptable_pa, mptable_pa);

    munmap(lowmem, MiB);

    boot_ip = REALMODE_BOOT_ADDR;

    return true;
}

int main(int argc, const char* argv[])
{
    Options options;

    parse_args(argc, argv, options);
    options.validate();

    AutoFd devmem = open("/dev/mem", O_RDWR);
    if (devmem < 0) {
        perror("Error: Failed to open /dev/mem");
        return 1;
    }

    void* slice_ram = mmap(nullptr, options.ramsize, PROT_READ | PROT_WRITE, MAP_SHARED, devmem, options.rambase);
    if (slice_ram == MAP_FAILED) {
        perror("Error: Failed to map slice RAM");
        return 1;
    }

    uintptr_t kernel_entry, kernel_arg;
    if (!load_linux(options, slice_ram, kernel_entry, kernel_arg))
        return 1;

    // TODO: zero-fill remaining slice RAM

    munmap(slice_ram, options.ramsize);

    uintptr_t boot_ip = UINTPTR_MAX;
    if (!lowmem_init(options, devmem, kernel_entry, kernel_arg, boot_ip))
        return 1;

    assert(boot_ip != UINTPTR_MAX);
    send_startup_ipi(devmem, options.apic_ids.front(), boot_ip);

    return 0;
}
