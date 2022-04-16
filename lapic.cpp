#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>

#include "runslice.h"

static constexpr uint32_t X2APIC_MSR_BASE = 0x800;
static constexpr uint32_t APIC_ID = 0x2;
static constexpr uint32_t APIC_ESR = 0x28;
static constexpr uint32_t APIC_ICR = 0x30;

static constexpr uint32_t APIC_ICR_DLV_STATUS = 0x1000;
static constexpr uint32_t APIC_ICR_DLV_MODE_INIT = 0x500;
static constexpr uint32_t APIC_ICR_DLV_MODE_STARTUP = 0x600;
static constexpr uint32_t APIC_ICR_LEVEL_ASSERT = 0x4000;
static constexpr uint32_t APIC_ICR_TRIGGER_LEVEL = 0x8000;

static bool rdmsr(const AutoFd& devmsr, uint32_t msrnum, uint64_t& result)
{
    if (pread(devmsr, &result, sizeof(result), msrnum) != sizeof(result)) {
        perror("Failed to read MSR");
        return false;
    }

    return true;
}

static bool wrmsr(const AutoFd& devmsr, uint32_t msrnum, uint64_t value)
{
    if (pwrite(devmsr, &value, sizeof(value), msrnum) != sizeof(value)) {
        perror("Failed to write MSR");
        return false;
    }

    return true;
}

class LocalApicBase
{
protected:
    virtual void send_ipi(uint32_t cmd, uint32_t dest, bool wait) = 0;

public:
    virtual ~LocalApicBase() = default;

    virtual uint32_t read_apic_id() = 0;

    void send_init_assert(uint32_t dest)
    {
        send_ipi(APIC_ICR_DLV_MODE_INIT | APIC_ICR_LEVEL_ASSERT | APIC_ICR_TRIGGER_LEVEL, dest, true);
    }

    void send_init_deassert(uint32_t dest)
    {
        send_ipi(APIC_ICR_DLV_MODE_INIT | APIC_ICR_TRIGGER_LEVEL, dest, true);
    }

    void send_startup(uint32_t dest, uint64_t startup_pa)
    {
        assert(startup_pa % 0x1000 == 0);
        assert(startup_pa < 0x100000);

        send_ipi(APIC_ICR_DLV_MODE_STARTUP | APIC_ICR_LEVEL_ASSERT | static_cast<uint32_t>(startup_pa >> 12), dest, true);
    }
};

class LocalApic : public LocalApicBase
{
protected:
    volatile void* m_base = nullptr;

    void write_reg(uint32_t reg, uint32_t value)
    {
        *reinterpret_cast<volatile uint32_t*>(static_cast<volatile char *>(m_base) + (reg << 4)) = value;
    }

    uint32_t read_reg(uint32_t reg)
    {
        return *reinterpret_cast<volatile uint32_t*>(static_cast<volatile char *>(m_base) + (reg << 4));
    }

    void send_ipi(uint32_t cmd, uint32_t dest, bool wait) override
    {
        assert(dest <= UINT8_MAX);

        // clear any errors
        write_reg(APIC_ESR, 0);

        // send the IPI
        // High and low halves are split. We must write the high portion first.
        assert(dest <= UINT8_MAX);
        write_reg(APIC_ICR + 1, dest << 24);
        write_reg(APIC_ICR, cmd);

        // Wait for delivery
        while(wait && (read_reg(APIC_ICR) & APIC_ICR_DLV_STATUS));
    }

public:
    LocalApic(void* base) : m_base(base) {}

    uint32_t read_apic_id()
    {
        return read_reg(APIC_ID) >> 24;
    }
};

class LocalX2Apic : public LocalApicBase
{
protected:
    AutoFd m_devmsr;

    void write_reg(uint32_t reg, uint64_t value)
    {
        bool ok = wrmsr(m_devmsr, X2APIC_MSR_BASE + reg, value);
        assert(ok);
    }

    uint64_t read_reg(uint32_t reg)
    {
        uint64_t result;
        bool ok = rdmsr(m_devmsr, X2APIC_MSR_BASE + reg, result);
        assert(ok);
        return result;
    }

    void send_ipi(uint32_t cmd, uint32_t dest, bool wait) override
    {
        (void)wait;

        // clear any errors
        write_reg(APIC_ESR, 0);

        // send the IPI
        write_reg(APIC_ICR, static_cast<uint64_t>(dest) << 32 | cmd);
    }

public:
    LocalX2Apic(AutoFd&& devmsr) : m_devmsr(std::move(devmsr)) {}

    uint32_t read_apic_id()
    {
        return read_reg(APIC_ID);
    }
};

static bool open_dev_msr(AutoFd& devmsr)
{
    // Ensure that there is only one CPU!
    std::filesystem::path devcpu("/dev/cpu");
    std::error_code err;
    std::filesystem::directory_iterator it(devcpu, err);
    if (err)
    {
        fprintf(stderr, "Failed to list /dev/cpu: %s\n", err.message().c_str());
        return false;
    }

    for (const auto& entry : it)
    {
        if (entry.is_directory())
        {
            if (devmsr >= 0)
            {
                fprintf(stderr, "Error: multiple CPUs found. We only support uniprocessor.\n");
                return false;
            }

            devmsr = open((entry.path().string() + "/msr").c_str(), O_RDWR);
            if (devmsr < 0) {
                perror("Failed to open /dev/cpu/*/msr");
                return false;
            }
        }
    }

    if (devmsr < 0)
    {
        fprintf(stderr, "Error: no CPUs found.\n");
        return false;
    }

    return true;
}

bool send_startup_ipi(AutoFd& devmem, uint32_t target_id, uint64_t startup_pa)
{
    AutoFd devmsr;
    if (!open_dev_msr(devmsr))
        return false;

    static constexpr uint32_t MSR_IA32_APIC_BASE = 0x1b;
    uint64_t apic_base_msr;
    if (!rdmsr(devmsr, MSR_IA32_APIC_BASE, apic_base_msr))
    {
        perror("Failed to read IA32_APIC_BASE MSR");
        return false;
    }

    assert(apic_base_msr & 0x900); // APIC enabled, is BSP

    void* apic_regs = nullptr;
    std::unique_ptr<LocalApicBase> lapic;

    if (apic_base_msr & 0x400)
    {
        printf("X2APIC mode\n");
        lapic = std::make_unique<LocalX2Apic>(std::move(devmsr));
    }
    else
    {
        uint64_t apic_base = apic_base_msr & ~0xfffUL;
        apic_regs = mmap(nullptr, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, devmem, apic_base);
        if (apic_regs == MAP_FAILED) {
            perror("Error: Failed to map APIC");
            return false;
        }

        lapic = std::make_unique<LocalApic>(apic_regs);
    }

    assert(lapic->read_apic_id() == get_local_apic_id());

    lapic->send_init_assert(target_id);
    lapic->send_init_deassert(target_id);
    lapic->send_startup(target_id, startup_pa);
    usleep(10);
    lapic->send_startup(target_id, startup_pa);

    if (apic_regs != nullptr)
        munmap(apic_regs, 0x1000);

    return true;
}
