#include <cstdint>
#include <vector>
#include <unistd.h>

#define ALIGN_UP(_v, _a)	(((_v) + (_a) - 1) & ~(static_cast<uintptr_t>(_a) - 1))

struct Options
{
    const char* kernel_path = nullptr;
    const char* initrd_path = nullptr;
    const char* kernel_cmdline = nullptr;
    const char* dsdt_path = nullptr;
    uint64_t rambase = 0;
    uint64_t ramsize = 0;
    uint64_t lowmem = 0x6000;
    std::vector<uint32_t> apic_ids;

    void validate();
};

class AutoFd
{
public:
    AutoFd() : m_fd(-1) {}
    AutoFd(int fd) : m_fd(fd) {}
    ~AutoFd() { if (m_fd >= 0) close(m_fd); }
    operator int() const { return m_fd; }
    int operator= (int fd) { if (m_fd >= 0) close(m_fd); m_fd = fd; return m_fd; }

private:
    int m_fd;
};

static inline void cpuid(uint32_t eax, uint32_t ecx, uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d)
{
    asm("cpuid"
        : "=a" (a), "=b" (b), "=c" (c), "=d" (d)
        : "a" (eax), "c" (ecx));
}

bool send_startup_ipi(AutoFd& devmem, uint32_t apic_id, uint64_t startup_pa);

uint8_t acpi_checksum(const void* data, size_t size);

uintptr_t build_acpi(
    const Options& options,
    uintptr_t& loadaddr_phys,
    char*& loadaddr_virt);

bool acpi_get_host_apic_ids(
    std::vector<uint32_t>& apic_ids);

bool read_to_devmem(std::ifstream& file, uint64_t offset, void* dest, size_t size);

bool load_linux(
    const Options& options,
    void* slice_ram,
    uintptr_t& kernel_entry_phys,
    uintptr_t& kernel_entry_arg);

bool lowmem_init(const Options& options, const AutoFd& devmem, uintptr_t kernel_entry, uintptr_t kernel_arg, uintptr_t &boot_ip);

extern "C" const size_t realmode_blob_size;
