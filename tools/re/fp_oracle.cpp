// Host-side answers from the same math the recompiler calls.
// stdin: one command per line. stdout: one hex line per command.
#include "cpu/softmath.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

static double hex_double(const char* s) {
    unsigned long long u = std::strtoull(s, nullptr, 16);
    double d;
    std::memcpy(&d, &u, 8);
    return d;
}

static void put_double(double d) {
    unsigned long long u;
    std::memcpy(&u, &d, 8);
    std::printf("%016llx\n", u);
}

// x86_ops.h x87_round_rc, RC = nearest-even. Kept here so the oracle does not
// need an X86Context. A zero result keeps the operand's sign.
static double round_nearest_even(double v) {
    if (!std::isfinite(v)) return v;
    double lo = std::floor(v);
    double fraction = v - lo;
    double rounded = lo;
    bool odd = false;
    if (std::fabs(lo) < 0x1p53) odd = ((std::int64_t)lo & 1) != 0;
    if (fraction > 0.5 || (fraction == 0.5 && odd)) rounded = lo + 1.0;
    return rounded == 0.0 ? std::copysign(0.0, v) : rounded;
}

int main() {
    char cmd[64];
    char a[96];
    char b[96];
    while (std::scanf("%63s", cmd) == 1) {
        if (std::strcmp(cmd, "x87sin") == 0 && std::scanf("%95s", a) == 1) {
            put_double(xbe_x87_sin(hex_double(a)));
        } else if (std::strcmp(cmd, "x87cos") == 0 && std::scanf("%95s", a) == 1) {
            put_double(xbe_x87_cos(hex_double(a)));
        } else if (std::strcmp(cmd, "x87tan") == 0 && std::scanf("%95s", a) == 1) {
            put_double(xbe_x87_tan(hex_double(a)));
        } else if (std::strcmp(cmd, "round24") == 0 && std::scanf("%95s", a) == 1) {
            put_double(xbe_round24(hex_double(a)));
        } else if (std::strcmp(cmd, "cvt") == 0 && std::scanf("%95s", a) == 1) {
            std::printf("%08x\n", (unsigned)xbe_cvt_i32(hex_double(a)));
        } else if (std::strcmp(cmd, "fistp") == 0 && std::scanf("%95s", a) == 1) {
            std::printf("%08x\n", (unsigned)xbe_cvt_i32(round_nearest_even(hex_double(a))));
        } else if (std::strcmp(cmd, "cvtt") == 0 && std::scanf("%95s", a) == 1) {
            double d = hex_double(a);
            std::printf("%08x\n", (unsigned)xbe_cvt_i32(std::trunc(d)));
        } else if (std::strcmp(cmd, "rng") == 0 && std::scanf("%95s", a) == 1) {
            unsigned s = (unsigned)std::strtoul(a, nullptr, 16);
            s = s * 0x343FDu + 0x269EC3u;
            std::printf("%08x %04x\n", s, (s >> 16) & 0x7FFFu);
        } else if (std::strcmp(cmd, "sincos") == 0 && std::scanf("%95s", a) == 1) {
            double s, c;
            xbe_x87_sincos(hex_double(a), &s, &c);
            unsigned long long us, uc;
            std::memcpy(&us, &s, 8);
            std::memcpy(&uc, &c, 8);
            std::printf("%016llx %016llx\n", us, uc);
        } else {
            std::fprintf(stderr, "oracle: bad command %s\n", cmd);
            return 2;
        }
        // A pipe is block-buffered. The differential tester reads one line
        // per command, so the answer has to leave the buffer before the next
        // scanf, or the two sides wait on each other.
        std::fflush(stdout);
        (void)b;
    }
    return 0;
}
