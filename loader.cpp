#include <cstring>
#include <fstream>
#include <iostream>

#include "linuxboot.h"
#include "runslice.h"

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

bool read_to_devmem(std::ifstream& file, uint64_t offset, void* dest, size_t size)
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

bool load_linux(
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
	boot_params->acpi_rsdp_addr = build_acpi(options, loadaddr_phys, loadaddr_virt);

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
