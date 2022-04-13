
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

uint8_t acpi_checksum(void* data, size_t size)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < size; i++)
        sum += static_cast<uint8_t*>(data)[i];
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
    fadt->Flags = ACPI_FADT_WBINVD | ACPI_FADT_HW_REDUCED;
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

    for (uint32_t apic_id : apic_ids) {
        acpi_madt_local_x2apic* lapic = alloc<acpi_madt_local_x2apic>(loadaddr_phys, loadaddr_virt);
        lapic->Header.Type = ACPI_MADT_TYPE_LOCAL_X2APIC;
        lapic->Header.Length = sizeof(*lapic);
        lapic->LocalApicId = apic_id;
        lapic->LapicFlags = ACPI_MADT_ENABLED;
    }

    fill_header(&madt->Header, ACPI_SIG_MADT, loadaddr_virt - reinterpret_cast<char*>(madt), 5);

    return madt_pa;
}

uintptr_t build_acpi(
    const Options& options,
    uintptr_t& loadaddr_phys,
    char*& loadaddr_virt)
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

    // Emit XSDT
    uintptr_t xsdt_pa = loadaddr_phys;
    acpi_table_xsdt* xsdt = alloc<acpi_table_xsdt>(loadaddr_phys, loadaddr_virt);

    // First entry is included in the size of the struct.
    static_assert(sizeof(xsdt->TableOffsetEntry) == sizeof(xsdt->TableOffsetEntry[0]));
    xsdt->TableOffsetEntry[0] = fadt_pa;

    alloc<uint64_t>(loadaddr_phys, loadaddr_virt);
    xsdt->TableOffsetEntry[1] = madt_pa;

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