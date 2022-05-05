
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <vector>

#include "runslice.h"

// Just enough ACPI-CA headers to define the tables
#include "external/acpi/acenv.h"
#include "external/acpi/actypes.h"
#include "external/acpi/actbl.h"

static const char* ACPI_OEM_ID = "SLICER";
static const char* ACPI_OEM_TABLE_ID = "SLICE   ";
static const char* ASL_COMPILER_ID = "SLDR";

template<typename T>
static inline T* alloc(uintptr_t& loadaddr_phys, char*& loadaddr_virt)
{
    T* t = reinterpret_cast<T*>(loadaddr_virt);
    memset(t, 0, sizeof(T));
    loadaddr_virt += sizeof(T);
    loadaddr_phys += sizeof(T);
    return t;
}

uint8_t acpi_checksum(const void* data, size_t size)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < size; i++)
        sum += static_cast<const uint8_t*>(data)[i];
    return 0 - sum;
}

template<typename T>
static inline void copy_id(T& dst, const char* src)
{
    assert(sizeof(dst) == strlen(src));
    memcpy(dst, src, sizeof(dst));
}

static void fill_header(
    ACPI_TABLE_HEADER* header,
    const char* signature,
    uint32_t length,
    uint8_t revision)
{
    copy_id(header->Signature, signature);
    header->Length = length;
    header->Revision = revision;
    copy_id(header->OemId, ACPI_OEM_ID);
    copy_id(header->OemTableId, ACPI_OEM_TABLE_ID);
    header->OemRevision = 1;
    copy_id(header->AslCompilerId, ASL_COMPILER_ID);
    header->AslCompilerRevision = 1;

    header->Checksum = acpi_checksum(header, length);
}

static uintptr_t emit_fadt(
    uintptr_t& loadaddr_phys,
    char*& loadaddr_virt,
    uintptr_t dsdt_pa)
{
    uintptr_t fadt_pa = loadaddr_phys;
    acpi_table_fadt* fadt = alloc<acpi_table_fadt>(loadaddr_phys, loadaddr_virt);

    fadt->BootFlags = ACPI_FADT_NO_VGA | ACPI_FADT_NO_CMOS_RTC;
    fadt->Flags = ACPI_FADT_WBINVD | ACPI_FADT_HW_REDUCED | ACPI_FADT_APIC_PHYSICAL;
    fadt->MinorRevision = 4;
    fadt->XDsdt = dsdt_pa;

    fill_header(&fadt->Header, ACPI_SIG_FADT, sizeof(acpi_table_fadt), 6);

    return fadt_pa;
}

static uintptr_t emit_madt(
    uintptr_t& loadaddr_phys,
    char*& loadaddr_virt,
    const std::vector<uint32_t>& apic_ids)
{
    constexpr uint32_t APIC_DEFAULT_ADDRESS = 0xfee00000;

    uintptr_t madt_pa = loadaddr_phys;
    acpi_table_madt* madt = alloc<acpi_table_madt>(loadaddr_phys, loadaddr_virt);

    madt->Address = APIC_DEFAULT_ADDRESS;
    madt->Flags = 0; // 8259 PICs not present

    uint32_t uid = 0;
    for (uint32_t apic_id : apic_ids) {
        acpi_madt_local_x2apic* lapic = alloc<acpi_madt_local_x2apic>(loadaddr_phys, loadaddr_virt);
        lapic->Header.Type = ACPI_MADT_TYPE_LOCAL_X2APIC;
        lapic->Header.Length = sizeof(*lapic);
        lapic->LocalApicId = apic_id;
        lapic->Uid = uid++;
        lapic->LapicFlags = ACPI_MADT_ENABLED;
    }

    fill_header(&madt->Header, ACPI_SIG_MADT, loadaddr_virt - reinterpret_cast<char*>(madt), 5);

    return madt_pa;
}

static uintptr_t emit_mcfg(
    uintptr_t& loadaddr_phys,
    char*& loadaddr_virt,
    uintptr_t& mmconfig_base)
{
    std::ifstream mcfg_file("/sys/firmware/acpi/tables/MCFG", std::ios::binary | std::ios::in);
    if (!mcfg_file.is_open()) {
        perror("Failed to open host MCFG file");
        return 0;
    }

    mcfg_file.seekg(0, std::ios::end);
    std::vector<char> mcfg_data(mcfg_file.tellg());
    mcfg_file.seekg(0, std::ios::beg);
    if (!mcfg_file.read(mcfg_data.data(), mcfg_data.size())) {
        perror("Failed to read host MCFG file");
        return 0;
    }

    const acpi_table_mcfg* const mcfg = reinterpret_cast<acpi_table_mcfg*>(mcfg_data.data());

    if (mcfg_data.size() < sizeof(*mcfg) ||
        0 != memcmp(mcfg->Header.Signature, ACPI_SIG_MCFG, sizeof(mcfg->Header.Signature)) ||
        mcfg->Header.Length != mcfg_data.size() ||
        (mcfg_data.size() - sizeof(*mcfg)) % sizeof(acpi_mcfg_allocation) != 0 ||
        0 != acpi_checksum(mcfg, mcfg_data.size()))
    {
        fprintf(stderr, "Invalid host MCFG file\n");
        return 0;
    }

    if (mcfg_data.size() != sizeof(*mcfg) + sizeof(acpi_mcfg_allocation))
    {
        fprintf(stderr, "Unsupported: host MCFG with multiple allocations\n");
        return 0;
    }

    const acpi_mcfg_allocation* mcfg_entry = reinterpret_cast<const acpi_mcfg_allocation*>(mcfg + 1);

    if (mcfg_entry->PciSegment != 0 || mcfg_entry->StartBusNumber != 0)
    {
        fprintf(stderr, "Unsupported: host MCFG with non-zero PCI segment or start bus number\n");
        return 0;
    }

    mmconfig_base = mcfg_entry->Address;

    uintptr_t mcfg_pa = loadaddr_phys;

    memcpy(loadaddr_virt, mcfg_data.data(), mcfg_data.size());

    loadaddr_phys += mcfg_data.size();
    loadaddr_virt += mcfg_data.size();

    return mcfg_pa;
}

uintptr_t build_acpi(
    const Options& options,
    uintptr_t& loadaddr_phys,
    char*& loadaddr_virt,
    uintptr_t& mmconfig_base)
{
    uintptr_t dsdt_pa = 0;

    if (options.dsdt_path != nullptr)
    {
        std::ifstream dsdt_file(options.dsdt_path, std::ios::binary | std::ios::in);
        if (!dsdt_file.is_open()) {
            perror("Failed to open DSDT AML file");
            return 0;
        }

        dsdt_file.seekg(0, std::ios::end);
        size_t dsdt_size = dsdt_file.tellg();

        if (!read_to_devmem(dsdt_file, 0, loadaddr_virt, dsdt_size)) {
            perror("Failed to read DSDT AML file");
            return 0;
        }

        dsdt_pa = loadaddr_phys;

        loadaddr_virt += dsdt_size;
        loadaddr_phys += dsdt_size;
    }

    uintptr_t fadt_pa = emit_fadt(loadaddr_phys, loadaddr_virt, dsdt_pa);
    uintptr_t madt_pa = emit_madt(loadaddr_phys, loadaddr_virt, options.apic_ids);
    uintptr_t mcfg_pa = emit_mcfg(loadaddr_phys, loadaddr_virt, mmconfig_base);
    if (mcfg_pa == 0) {
        return 0;
    }

    // Emit XSDT
    uintptr_t xsdt_pa = loadaddr_phys;
    acpi_table_xsdt* xsdt = alloc<acpi_table_xsdt>(loadaddr_phys, loadaddr_virt);

    // First entry is included in the size of the struct.
    int i = 0;
    static_assert(sizeof(xsdt->TableOffsetEntry) == sizeof(xsdt->TableOffsetEntry[0]));
    xsdt->TableOffsetEntry[i++] = fadt_pa;

    alloc<uint64_t>(loadaddr_phys, loadaddr_virt);
    xsdt->TableOffsetEntry[i++] = madt_pa;

    alloc<uint64_t>(loadaddr_phys, loadaddr_virt);
    xsdt->TableOffsetEntry[i++] = mcfg_pa;

    fill_header(&xsdt->Header, ACPI_SIG_XSDT, loadaddr_virt - reinterpret_cast<char*>(xsdt), 1);

    // Emit RSDP
    uintptr_t rsdp_pa = loadaddr_phys;
    acpi_table_rsdp* rsdp = alloc<acpi_table_rsdp>(loadaddr_phys, loadaddr_virt);
    copy_id(rsdp->Signature, ACPI_SIG_RSDP);
    rsdp->Checksum = acpi_checksum(rsdp, offsetof(acpi_table_rsdp, Length));
    copy_id(rsdp->OemId, ACPI_OEM_ID);
    rsdp->Revision = 2;
    rsdp->Length = sizeof(*rsdp);
    rsdp->XsdtPhysicalAddress = xsdt_pa;
    rsdp->ExtendedChecksum = acpi_checksum(rsdp, sizeof(*rsdp));

    return rsdp_pa;
}

bool acpi_get_host_apic_ids(
    std::vector<uint32_t>& apic_ids)
{
    std::ifstream madt_file("/sys/firmware/acpi/tables/APIC", std::ios::binary | std::ios::in);
    if (!madt_file.is_open()) {
        perror("Failed to open host MADT file");
        return false;
    }

    madt_file.seekg(0, std::ios::end);
    std::vector<char> madt_data(madt_file.tellg());
    madt_file.seekg(0, std::ios::beg);
    if (!madt_file.read(madt_data.data(), madt_data.size())) {
        perror("Failed to read host MADT file");
        return false;
    }

    const acpi_table_madt* const madt = reinterpret_cast<acpi_table_madt*>(madt_data.data());

    if (madt_data.size() < sizeof(*madt) ||
        0 != memcmp(madt->Header.Signature, ACPI_SIG_MADT, sizeof(madt->Header.Signature)) ||
        madt->Header.Length != madt_data.size() ||
        0 != acpi_checksum(madt, madt_data.size()))
    {
        fprintf(stderr, "Invalid host MADT file\n");
        return false;
    }

    apic_ids.clear();

    for (
        const ACPI_SUBTABLE_HEADER* entry = reinterpret_cast<const ACPI_SUBTABLE_HEADER*>(madt + 1);
        reinterpret_cast<const char*>(entry) <= madt_data.data() + madt_data.size()
            && reinterpret_cast<const char*>(entry) + sizeof(*entry) <= madt_data.data() + madt_data.size()
            && reinterpret_cast<const char*>(entry) + entry->Length <= madt_data.data() + madt_data.size();
        entry = reinterpret_cast<const ACPI_SUBTABLE_HEADER*>(reinterpret_cast<const char*>(entry) + entry->Length))
    {
        switch (entry->Type)
        {
        case ACPI_MADT_TYPE_LOCAL_APIC:
        {
            const acpi_madt_local_apic* lapic = reinterpret_cast<const acpi_madt_local_apic*>(entry);
            if (entry->Length != sizeof(*lapic)) {
                fprintf(stderr, "Invalid host ACPI_MADT_LOCAL_APIC entry\n");
                return false;
            } else if (lapic->LapicFlags & ACPI_MADT_ENABLED) {
                apic_ids.push_back(lapic->Id);
            }
            break;
        }

        case ACPI_MADT_TYPE_LOCAL_X2APIC:
        {
            const acpi_madt_local_x2apic* x2apic = reinterpret_cast<const acpi_madt_local_x2apic*>(entry);
            if (entry->Length != sizeof(*x2apic)) {
                fprintf(stderr, "Invalid host ACPI_MADT_LOCAL_X2APIC entry\n");
                return false;
            } else if (x2apic->LapicFlags & ACPI_MADT_ENABLED) {
                apic_ids.push_back(x2apic->LocalApicId);
            }
            break;
        }

        default:
            break;
        }
    }

    return true;
}
