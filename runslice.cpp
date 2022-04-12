#include <fcntl.h>
#include <sys/mman.h>
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
        << "  -cpus APICIDS   Comma-separated list of APIC IDs." << std::endl;

    exit(1);
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
    if (apic_ids.empty())
        usage("APIC IDs are required");
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
        } else if (strcmp(argv[i], "-cpus") == 0) {
            if (++i >= argc)
                usage();
            parse_cpus(argv[i], options.apic_ids);
        } else {
            usage();
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
