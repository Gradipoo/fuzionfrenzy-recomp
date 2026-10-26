// Cross-arch digest of the bundled guest math. Not linked into the game.
// Build twice, once per architecture, and compare the printed hash.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>

#include "cpu/softmath.h"

static uint64_t g_hash = 14695981039346656037ull;

static void Mix(uint64_t v) { g_hash = (g_hash ^ v) * 0x100000001B3ull; }

static uint64_t Bits(double d) {
    uint64_t u;
    std::memcpy(&u, &d, 8);
    return u;
}

static uint64_t BitsF(float d) {
    uint32_t u;
    std::memcpy(&u, &d, 4);
    return u;
}

static double In(double x) {
    volatile double v = x;
    return v;
}

static float InF(float x) {
    volatile float v = x;
    return v;
}

static int g_fail = 0;

static void Expect(const char* name, bool ok) {
    if (ok) return;
    std::fprintf(stderr, "FAIL %s\n", name);
    g_fail++;
}

static void Feed(double y) { Mix(Bits(y)); }
static void FeedF(float y) { Mix(BitsF(y)); }

int main() {
    xbe_arm_host_fp();
    Expect("sin0", xbe_sin(0) == 0);
    Expect("cos0", xbe_cos(0) == 1);
    Expect("exp0", xbe_exp(0) == 1);
    Expect("log2_2", xbe_log2(2) == 1);
    Expect("log2_1", xbe_log2(1) == 0);
    Expect("log2_4", xbe_log2(4) == 2);
    Expect("pow_m2_3", xbe_pow(-2, 3) == -8);
    Expect("pow_10_3", xbe_pow(10, 3) == 1000);
    Expect("pow_2_10", xbe_pow(2, 10) == 1024);
    Expect("ldexp", xbe_ldexp(1.5, -1) == 0.75);
    Expect("ldexp_min", Bits(xbe_ldexp(1.0, -1074)) == 1ull);
    int e = 0;
    Expect("frexp2", xbe_frexp(2, &e) == 0.5 && e == 2);
    double s = xbe_sin(In(0x1.921fb54442d18p+0));
    double c = xbe_cos(In(0x1.921fb54442d18p+0));
    Expect("sin_pi2", std::fabs(s - 1) < 1e-12);
    Expect("cos_pi2", std::fabs(c) < 1e-12);
    Expect("sin1", std::fabs(xbe_sin(In(1)) - std::sin(1)) < 1e-9);
    Expect("cos1", std::fabs(xbe_cos(In(1)) - std::cos(1)) < 1e-9);
    Expect("exp1", std::fabs(xbe_exp(In(1)) - std::exp(1)) < 1e-9);
    Expect("log10", std::fabs(xbe_log(In(10)) - std::log(10)) < 1e-9);
    Expect("pow10", std::fabs(xbe_pow(In(10), In(-0.5)) - std::pow(10, -0.5)) < 1e-9);
    Expect("atan1", std::fabs(xbe_atan(In(1)) - std::atan(1)) < 1e-9);
    double pi = 0x1.921fb54442d18p+1;
    double fsin_pi = xbe_x87_sin(In(pi));
    double lib_pi = xbe_sin(In(pi));
    Expect("x87_sin_small", std::fabs(xbe_x87_sin(In(0.2)) - xbe_sin(In(0.2))) == 0.0);
    Expect("fsin_pi_differs", fsin_pi != lib_pi);
    Expect("fsin_pi", std::fabs(fsin_pi - 1.2246063538223773e-16) < 1e-31);
    double huge = xbe_ldexp(0x1.000002p0, 200);
    double r24 = xbe_round24(huge);
    Expect("round24_finite", std::isfinite(r24) && r24 != 0.0);
    Expect("round24_narrow", xbe_round24(1.0000001) == (double)(float)1.0000001);
    Expect("cvt_max", xbe_cvt_i32(2147483647.0) == 2147483647);
    Expect("cvt_oor", xbe_cvt_i32(2147483648.0) == (int32_t)0x80000000u);
    Expect("cvt_nan", xbe_cvt_i32(std::numeric_limits<double>::quiet_NaN()) == (int32_t)0x80000000u);
    Expect("trunc_clamp", xbe_trunc_i32(1e20) == 4096);

    double samples[] = {0, -0.0, 0.25, 0.5, 1, -1, 2, 10, -8, 0x1p-1074, 0x1p-1022, 0x1.fffffp+127,
                        0x1.921fb54442d18p+0, 0x1.921fb54442d18p+1, 100, 1000, 1e-8, 0.1};
    for (double x : samples) {
        double a = In(x);
        Feed(xbe_sin(a));
        Feed(xbe_cos(a));
        Feed(xbe_tan(a));
        Feed(xbe_exp(a));
        Feed(xbe_exp2(a));
        if (a > 0) {
            Feed(xbe_log(a));
            Feed(xbe_log2(a));
            Feed(xbe_log10(a));
        }
        Feed(xbe_atan(a));
        if (a >= 0) Feed(std::sqrt(a));
        uint8_t ext[10];
        xbe_store80(ext, a);
        double back = xbe_load80(ext);
        Expect("roundtrip", Bits(back) == Bits(a) || (a != a && back != back));
        Mix(Bits(back));
        for (int i = 0; i < 10; i++) Mix(ext[i]);
    }
    // A significand whose low 11 bits are set must round the same way everywhere.
    uint8_t odd[10] = {0xff, 0xff, 0, 0, 0, 0, 0, 0x80, 0xff, 0x3f};
    Feed(xbe_load80(odd));

    float fma_a = InF(0x1.000002p0f);
    float fma_b = InF(0x1.000002p0f);
    float fma_c = InF(-0x1.000000p0f);
    float acc = 0;
    for (int i = 0; i < 64; i++) acc = fma_a * acc + fma_b * fma_c;
    FeedF(fma_a * fma_b + fma_c);
    FeedF(acc);
    FeedF(InF(0x1p-140f));
    FeedF(InF(0x1p-149f) * InF(0.5f));
    FeedF(xbe_powf(10.f, -1.5f));
    FeedF(xbe_sinf(0.7f));
    FeedF(xbe_log2f(3.f));
    FeedF(xbe_exp2f(-0.3f));
    Feed(xbe_fmod(5.5, 2));
    Feed(xbe_fmod(-5.5, 2));
    Feed(xbe_remainder(5.5, 2));
    Feed(xbe_asin(0.5));
    Feed(xbe_acos(0.5));
    Feed(xbe_atan2(-1, -1));
    Feed(xbe_x87_sin(In(pi)));
    Feed(xbe_x87_cos(In(pi)));
    Feed(xbe_x87_tan(In(1.0)));
    Feed(r24);
    Mix(uint32_t(xbe_cvt_i32(2147483648.0)));
    Mix(uint32_t(xbe_cvt_i32(-2147483649.0)));

    std::printf("%016llx %s\n", (unsigned long long)g_hash, g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
