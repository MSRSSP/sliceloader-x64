#include <cstdint>
#include <vector>

bool send_startup_ipi(AutoFd& devmem, uint32_t apic_id, uint64_t startup_pa);

uint8_t acpi_checksum(void* data, size_t size);

uintptr_t build_acpi(
    uintptr_t& loadaddr_phys,
    char*& loadaddr_virt,
    uint64_t rambase,
    uint64_t ramsize,
    const std::vector<uint32_t>& apic_ids);
