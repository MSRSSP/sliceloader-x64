#include <fcntl.h>
#include <sys/mman.h>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>

#include "runslice.h"

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
        << "  -lowmem ADDR    Physical address of low memory used for boot." << std::endl
        << "  -cpus APICIDS   Comma-separated list of APIC IDs." << std::endl
        << "  -dsdt FILE      ACPI DSDT AML file." << std::endl;

    exit(1);
}

// XXX: This assumes we're on a uniprocessor (!!)
uint32_t get_local_apic_id()
{
    uint32_t apic_id = UINT32_MAX;
    {
        uint32_t max_cpuid_leaf, a, b, c, d;
        cpuid(0, 0, max_cpuid_leaf, b, c, d);

        assert(max_cpuid_leaf >= 0xb);
        cpuid(0xb, 0, a, b, c, apic_id);

        if (max_cpuid_leaf >= 0x1f)
        {
            cpuid(0x1f, 0, a, b, c, d);
            assert(d == apic_id);
        }
    }

    return apic_id;
}

static bool validate_apic_ids(const std::vector<uint32_t>& slice_ids)
{
    std::vector<uint32_t> host_ids;
    if (!acpi_get_host_apic_ids(host_ids))
        return false;

    uint32_t bsp_apic_id = get_local_apic_id();

    // Print the host APIC IDs.
    std::cout << "Host APIC IDs: ";
    for (uint32_t id : host_ids) {
        std::cout << id << (id == bsp_apic_id ? "(BSP) " : " ");
    }
    std::cout << std::endl;

    assert(std::find(host_ids.begin(), host_ids.end(), bsp_apic_id) != host_ids.end());

    // Check that the slice IDs are valid, and not duplicated.
    for (uint32_t id : slice_ids) {
        if (id == bsp_apic_id) {
            fprintf(stderr, "Error: APIC ID %u is the BSP\n", id);
            return false;
        }
        assert(id != UINT32_MAX);
        auto it = std::find(host_ids.begin(), host_ids.end(), id);
        if (it == host_ids.end()) {
            fprintf(stderr, "Error: APIC ID %u duplicated or not present\n", id);
            return false;
        }
        *it = UINT32_MAX; // mark invalid to catch duplicates
    }

    return true;
}

void Options::validate()
{
    if (kernel_path == nullptr)
        usage("Kernel image path is required");
    if (rambase == 0 || ramsize == 0)
        usage("RAM base and size are required");
    if (rambase % 0x1000 != 0)
        usage("RAM base must be page-aligned");
    if (ramsize % 0x1000 != 0)
        usage("RAM size must be page-aligned");
    if (lowmem > 640 * 1024 - realmode_blob_size)
        usage("Low memory must fit below 640K");
    if (lowmem % 0x1000 != 0)
        usage("Low memory must be page-aligned");
    if (apic_ids.empty())
        usage("APIC IDs are required");
    if (!validate_apic_ids(apic_ids))
        usage("Invalid APIC IDs");
}

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
        } else if (strcmp(argv[i], "-lowmem") == 0) {
            if (++i >= argc)
                usage();
            options.lowmem = strtoul(argv[i], nullptr, 0);
        } else if (strcmp(argv[i], "-cpus") == 0) {
            if (++i >= argc)
                usage();
            parse_cpus(argv[i], options.apic_ids);
        } else if (strcmp(argv[i], "-dsdt") == 0) {
            if (++i >= argc)
                usage();
            options.dsdt_path = argv[i];
        } else {
            usage("Unrecognised option");
        }
    }
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
