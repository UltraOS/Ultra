#include <sstream>
#include <cstdio>
#include <cinttypes>

#include <test_harness.h>

extern "C" {

#include <log.h>
#include <boot/boot.h>
#include <panic.h>

}

struct boot_context g_boot_ctx;

void vprint(const char *msg, va_list vlist)
{
    std::vprintf(msg, vlist);
}

void print(const char *msg, ...)
{
    va_list vlist;
    va_start(vlist, msg);
    vprint(msg, vlist);
    va_end(vlist);
}

void panic(const char *msg, ...)
{
    char buf[4096];
    std::stringstream ss;

    va_list vlist;
    va_start(vlist, msg);
    std::vsnprintf(buf, sizeof(buf), msg, vlist);
    va_end(vlist);

    ss << buf;

    throw std::runtime_error(ss.str());
}

void do_assert_eq(uint64_t lhs, uint64_t rhs, const char *file, size_t line)
{
    if (lhs == rhs)
        return;

    panic(
        "Assertion failed: %" PRIX64 " != %" PRIX64 " at %s:%zu\n",
        lhs, rhs, file, line
    );
}
