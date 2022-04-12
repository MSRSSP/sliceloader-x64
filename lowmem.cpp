#include <sys/mman.h>
#include <cassert>
#include <cstring>
#include <iostream>

#include "mptable.h"
#include "runslice.h"

extern "C" char realmode_blob_start[];
extern "C" size_t realmode_blob_size;

struct realmode_header {
    uint64_t reserved;
    uint64_t kernel_entry;
    uint64_t kernel_arg;
} __attribute__((__packed__));

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

    MPConfigHeader* mpt = reinterpret_cast<MPConfigHeader*>(mpfp + 1);
    memset(mpt, 0, sizeof(*mpt));
    mpt->signature = MPTABLE_CONFIG_MAGIC;
    // base_length and checksum are computed at the end
    mpt->spec_rev = 4;
    memset(mpt->oemid, ' ', sizeof(mpt->oemid));
    memset(mpt->prodid, ' ', sizeof(mpt->oemid));
    memcpy(mpt->oemid, "SLICER", 6);
    memcpy(mpt->prodid, "SLICER", 6);
    mpt->lapic_addr = 0xfee00000;
    mpt->entries = 0;

    // Assume uniform CPUs.
    uint16_t family_model_stepping;
    uint32_t cpu_feature_flags;
    {
        uint32_t eax, ebx, ecx;
        cpuid(1, 0, eax, ebx, ecx, cpu_feature_flags);
        family_model_stepping = eax & 0xfff;
    }

    // Emit one processor entry for each CPU.
    mpt->entries += options.apic_ids.size();
    MPProcessorEntry* mpp = reinterpret_cast<MPProcessorEntry*>(mpt + 1);
    for (size_t i = 0; i < options.apic_ids.size(); i++)
    {
        mpp[i].type = MPEntryType::Processor;
        mpp[i].apic_id = options.apic_ids[i];
        mpp[i].apic_ver = 0x14;
        mpp[i].cpu_flags = MP_PROCESSOR_ENABLED;
        if (i == 0) {
            mpp[i].cpu_flags |= MP_PROCESSOR_BSP;
        }
        mpp[i].cpu_signature = family_model_stepping;
        mpp[i].feature_flags = cpu_feature_flags;
        memset(&mpp[i].reserved, 0, sizeof(mpp[i].reserved));
    }

    // Emit NMI routing.
    mpt->entries++;
    MPInterruptEntry* mpi = reinterpret_cast<MPInterruptEntry*>(mpp + options.apic_ids.size());
    mpi->type = MPEntryType::LocalInterrupt;
    mpi->int_type = MPInterruptType::Nmi;
    mpi->flags = 0;
    mpi->source_bus = 0;
    mpi->source_irq = 0;
    mpi->dest_apic_id = 0xff;
    mpi->dest_apic_int = 1;

    // Compute total length and checksum.
    mpt->base_length = reinterpret_cast<char*>(mpi + 1) - reinterpret_cast<char*>(mpt);
    mpt->checksum = acpi_checksum(mpt, mpt->base_length);
}

bool lowmem_init(const Options& options, const AutoFd& devmem, uintptr_t kernel_entry, uintptr_t kernel_arg, uintptr_t &boot_ip)
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
