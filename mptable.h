#include <cstdint>

constexpr uint32_t MPTABLE_PTR_MAGIC = ('_'<<24) | ('P'<<16) | ('M'<<8) | '_';

struct MPFloatingPointer
{
    uint32_t signature;
    uint32_t physaddr;
    uint8_t length;
    uint8_t specrev;
    uint8_t checksum;
    uint8_t features[5];
};
static_assert(sizeof(MPFloatingPointer) == 16);

constexpr uint32_t MPTABLE_CONFIG_MAGIC = ('P'<<24) | ('M'<<16) | ('C'<<8) | 'P';

struct MPConfigHeader
{
    uint32_t signature;
    uint16_t base_length;
    uint8_t spec_rev;
    uint8_t checksum;
    char oemid[8];
    char prodid[12];
    uint32_t oem_table_addr;
    uint16_t oem_table_size;
    uint16_t entries;
    uint32_t lapic_addr;
    uint16_t ext_length;
    uint8_t ext_checksum;
    uint8_t reserved;
};
static_assert(sizeof(MPConfigHeader) == 44);

enum class MPEntryType : uint8_t
{
    Processor = 0,
    Bus = 1,
    IOAPIC = 2,
    IOInterrupt = 3,
    LocalInterrupt = 4,
};

struct MPProcessorEntry
{
    MPEntryType type;
    uint8_t apic_id;
    uint8_t apic_ver;
    uint8_t cpu_flags;
    uint32_t cpu_signature;
    uint32_t feature_flags;
    uint32_t reserved[2];
};

constexpr uint8_t MP_PROCESSOR_ENABLED = 1;
constexpr uint8_t MP_PROCESSOR_BSP = 2;
