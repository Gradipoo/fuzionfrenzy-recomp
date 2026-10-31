// Report fields advance the XInput packet. A padding byte does not.
#include "det/xinput_packet.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

struct Report {
    bool connected;
    uint16_t buttons;
    uint8_t analog[8];
    int16_t lx, ly, rx, ry;
};

struct Padded {
    bool connected;
    char pad;
    uint16_t buttons;
    uint8_t analog[8];
    int16_t lx, ly, rx, ry;
};

static int g_fails = 0;

static void Expect(bool ok, const char* what) {
    if (ok) return;
    std::fprintf(stderr, "FAIL %s\n", what);
    g_fails++;
}

static Report Base() {
    Report r{};
    r.connected = true;
    r.buttons = 0x1000;
    r.analog[0] = 1;
    r.lx = 10;
    r.ly = -10;
    r.rx = 20;
    r.ry = -20;
    return r;
}

int main() {
    Report a = Base();
    Report b = Base();
    Expect(!det::XInputReportChanged(a, b), "identical reports");
    b.connected = false;
    Expect(det::XInputReportChanged(a, b), "connected");
    b = Base();
    b.buttons = 1;
    Expect(det::XInputReportChanged(a, b), "buttons");
    b = Base();
    b.analog[3] = 9;
    Expect(det::XInputReportChanged(a, b), "analog");
    b = Base();
    b.lx = 11;
    Expect(det::XInputReportChanged(a, b), "lx");
    b = Base();
    b.ly = 0;
    Expect(det::XInputReportChanged(a, b), "ly");
    b = Base();
    b.rx = 0;
    Expect(det::XInputReportChanged(a, b), "rx");
    b = Base();
    b.ry = 0;
    Expect(det::XInputReportChanged(a, b), "ry");

    Padded p{};
    Padded q{};
    p.connected = q.connected = true;
    p.pad = 0;
    q.pad = 0x5A;
    Expect(std::memcmp(&p, &q, sizeof p) != 0, "padding makes memcmp differ");
    Expect(!det::XInputReportChanged(p, q), "padding is not a report field");

    if (g_fails) {
        std::fprintf(stderr, "test_xinput_packet: %d failed\n", g_fails);
        return 1;
    }
    std::printf("test_xinput_packet: PASS\n");
    return 0;
}
