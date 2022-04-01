#include <unistd.h>

#define ALIGN_UP(_v, _a)	(((_v) + (_a) - 1) & ~(static_cast<uintptr_t>(_a) - 1))

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
