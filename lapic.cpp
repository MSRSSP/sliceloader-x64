#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>

#include "utils.h"
#include "runslice.h"

static constexpr uint32_t APIC_ID = 0x20;
static constexpr uint32_t APIC_ESR = 0x280;
static constexpr uint32_t APIC_ICR_LO = 0x300;
static constexpr uint32_t APIC_ICR_HI = 0x310;

static constexpr uint32_t APIC_ICR_DLV_STATUS = 0x1000;
static constexpr uint32_t APIC_ICR_DLV_MODE_INIT = 0x500;
static constexpr uint32_t APIC_ICR_DLV_MODE_STARTUP = 0x600;
static constexpr uint32_t APIC_ICR_LEVEL_ASSERT = 0x4000;
static constexpr uint32_t APIC_ICR_TRIGGER_LEVEL = 0x8000;

class LocalApic
{
private:
    volatile void* m_base = nullptr;

    void write_reg(uintptr_t reg, uint32_t value)
    {
        *reinterpret_cast<volatile uint32_t*>(static_cast<volatile char *>(m_base) + reg) = value;
    }

    uint32_t read_reg(uintptr_t reg)
    {
        return *reinterpret_cast<volatile uint32_t*>(static_cast<volatile char *>(m_base) + reg);
    }

    void send_ipi(uint32_t cmd, uint8_t dest, bool wait)
    {
        // clear any errors
        write_reg(APIC_ESR, 0);

        //send the IPI
        write_reg(APIC_ICR_HI, static_cast<uint32_t>(dest) << 24);
        write_reg(APIC_ICR_LO, cmd);

        // Wait for delivery
        while(wait && (read_reg(APIC_ICR_LO) & APIC_ICR_DLV_STATUS));
    }

public:
    LocalApic(void* base) : m_base(base) {}

    uint8_t read_apic_id()
    {
        return read_reg(APIC_ID) >> 24;
    }

    void send_init_assert(uint8_t dest)
    {
        send_ipi(APIC_ICR_DLV_MODE_INIT | APIC_ICR_LEVEL_ASSERT | APIC_ICR_TRIGGER_LEVEL, dest, true);
    }

    void send_init_deassert(uint8_t dest)
    {
        send_ipi(APIC_ICR_DLV_MODE_INIT | APIC_ICR_TRIGGER_LEVEL, dest, true);
    }

    void send_startup(uint8_t dest, uint64_t startup_pa)
    {
        assert(startup_pa % 0x1000 == 0);
        assert(startup_pa < 0x100000);

        send_ipi(APIC_ICR_DLV_MODE_STARTUP | APIC_ICR_LEVEL_ASSERT | static_cast<uint32_t>(startup_pa >> 12), dest, true);
    }
};

static uint64_t read_apic_base()
{
    static constexpr uint32_t MSR_IA32_APIC_BASE = 0x1b;
    uint64_t apic_base_msr;

    // Ensure that there is only one CPU!
    std::filesystem::path devcpu("/dev/cpu");
    std::error_code err;
    std::filesystem::directory_iterator it(devcpu, err);
    if (err)
    {
        fprintf(stderr, "Failed to list /dev/cpu: %s\n", err.message().c_str());
        return 0;
    }

    AutoFd devmsr;
    for (const auto& entry : it)
    {
        if (devmsr >= 0)
        {
            fprintf(stderr, "Error: multiple CPUs found. We only support uniprocessor.\n");
            return 0;
        }

        devmsr = open((entry.path().string() + "/msr").c_str(), O_RDWR);
        if (devmsr < 0) {
            perror("Failed to open /dev/cpu/*/msr");
            return 0;
        }
    }

    if (devmsr < 0)
    {
        fprintf(stderr, "Error: no CPUs found.\n");
        return 0;
    }

    lseek(devmsr, MSR_IA32_APIC_BASE, SEEK_SET);

    if (read(devmsr, &apic_base_msr, sizeof(apic_base_msr)) != sizeof(apic_base_msr)) {
        perror("Failed to read IA32_APIC_BASE MSR");
        return 0;
    }

    assert(apic_base_msr & 0x900); // APIC enabled, is BSP

    return apic_base_msr & ~0xfffUL;
}

bool send_startup_ipi(AutoFd& devmem, uint32_t apic_id, uint64_t startup_pa)
{
    uint64_t apic_base = read_apic_base();
    if (!apic_base)
        return false;

    void* apic_regs = mmap(nullptr, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, devmem, apic_base);
    if (apic_regs == MAP_FAILED) {
        perror("Error: Failed to map APIC");
        return false;
    }

    assert(apic_id <= UINT8_MAX);

    LocalApic local_apic(apic_regs);

    uint8_t local_apic_id = local_apic.read_apic_id();
    printf("My local APIC ID is %u\n", local_apic_id);
    assert(apic_id != local_apic_id);

    uint32_t a, b, c, d;
    cpuid(1, 0, a, b, c, d);
    assert(b >> 24 == local_apic_id);

    printf("Send INIT assert\n");
    local_apic.send_init_assert(static_cast<uint8_t>(apic_id));
    printf("Send INIT deassert\n");
    local_apic.send_init_deassert(static_cast<uint8_t>(apic_id));
    printf("Send startup IPI\n");
    local_apic.send_startup(static_cast<uint8_t>(apic_id), startup_pa);
    //printf("Done!\n");

    return true;
}