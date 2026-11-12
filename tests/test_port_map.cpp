// Public addresses are the only ones Host online may put in a link.
#include "net/port_map.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

static int g_fails = 0;

static void Expect(bool ok, const char* what) {
    if (ok) return;
    std::fprintf(stderr, "FAIL %s\n", what);
    g_fails++;
}

int main() {
    Expect(net::PublicIPv4("8.8.8.8"), "8.8.8.8");
    Expect(net::PublicIPv4("1.2.3.4"), "1.2.3.4");
    Expect(net::PublicIPv4("172.15.255.255"), "172.15");
    Expect(net::PublicIPv4("172.32.0.1"), "172.32");
    Expect(net::PublicIPv4("100.63.255.255"), "below cgnat");
    Expect(net::PublicIPv4("100.128.0.1"), "above cgnat");
    Expect(net::PublicIPv4("192.169.0.1"), "192.169");
    Expect(!net::PublicIPv4("10.0.0.1"), "10/8");
    Expect(!net::PublicIPv4("10.255.255.255"), "10/8 top");
    Expect(!net::PublicIPv4("172.16.0.1"), "172.16");
    Expect(!net::PublicIPv4("172.31.255.255"), "172.31");
    Expect(!net::PublicIPv4("192.168.1.1"), "192.168");
    Expect(!net::PublicIPv4("100.64.0.1"), "cgnat start");
    Expect(!net::PublicIPv4("100.127.255.255"), "cgnat end");
    Expect(!net::PublicIPv4("127.0.0.1"), "loopback");
    Expect(!net::PublicIPv4("127.255.255.255"), "loopback top");
    Expect(!net::PublicIPv4("0.0.0.0"), "unspecified");
    Expect(!net::PublicIPv4("169.254.1.1"), "link local");
    Expect(!net::PublicIPv4("224.0.0.1"), "multicast");
    Expect(!net::PublicIPv4("255.255.255.255"), "broadcast");
    Expect(!net::PublicIPv4(""), "empty");
    Expect(!net::PublicIPv4("8.8.8"), "short");
    Expect(!net::PublicIPv4("8.8.8.8 "), "trailing");
    Expect(!net::PublicIPv4("999.1.1.1"), "octet");

    uint8_t nonce[12];
    for (int i = 0; i < 12; i++) nonce[i] = uint8_t(0x10 + i);
    uint8_t pkt[60];
    Expect(net::BuildPcpMap(pkt, 60, nonce, "203.0.113.8", 9999, 9999, 3600), "pcp build");
    Expect(pkt[0] == 2 && pkt[1] == 1, "pcp version opcode");
    Expect(pkt[2] == 0 && pkt[3] == 0, "pcp reserved");
    Expect(pkt[4] == 0 && pkt[5] == 0 && pkt[6] == 0x0e && pkt[7] == 0x10, "pcp lifetime");
    bool prefix = true;
    for (int i = 8; i < 18; i++)
        if (pkt[i] != 0) prefix = false;
    Expect(prefix && pkt[18] == 0xff && pkt[19] == 0xff, "pcp mapped prefix");
    Expect(pkt[20] == 203 && pkt[21] == 0 && pkt[22] == 113 && pkt[23] == 8, "pcp client");
    Expect(std::memcmp(pkt + 24, nonce, 12) == 0, "pcp nonce");
    Expect(pkt[36] == 17 && pkt[37] == 0 && pkt[38] == 0 && pkt[39] == 0, "pcp udp");
    Expect(pkt[40] == 0x27 && pkt[41] == 0x0f, "pcp internal");
    Expect(pkt[42] == 0x27 && pkt[43] == 0x0f, "pcp suggest");
    bool suggest0 = true;
    for (int i = 44; i < 60; i++)
        if (pkt[i] != 0) suggest0 = false;
    Expect(suggest0, "pcp suggest ip");
    Expect(!net::BuildPcpMap(pkt, 59, nonce, "203.0.113.8", 9999, 9999, 3600), "pcp short out");
    Expect(!net::BuildPcpMap(pkt, 60, nonce, "203.0.113", 9999, 9999, 3600), "pcp bad client");
    Expect(!net::BuildPcpMap(pkt, 60, nonce, "203.0.113.8", 0, 0, 0), "pcp port 0");

    uint8_t del[60];
    Expect(net::BuildPcpMap(del, 60, nonce, "203.0.113.8", 9999, 0, 0), "pcp delete build");
    Expect(del[0] == 2 && del[1] == 1, "pcp delete still version 2");
    Expect(del[4] == 0 && del[5] == 0 && del[6] == 0 && del[7] == 0, "pcp delete lifetime");
    Expect(del[40] == 0x27 && del[41] == 0x0f && del[42] == 0 && del[43] == 0, "pcp delete ports");
    Expect(std::memcmp(del + 24, nonce, 12) == 0, "pcp delete nonce");

    uint8_t resp[64];
    std::memset(resp, 0, sizeof resp);
    resp[0] = 2;
    resp[1] = 0x81;
    resp[6] = 0x0e;
    resp[7] = 0x10;
    std::memcpy(resp + 24, nonce, 12);
    resp[36] = 17;
    resp[40] = 0x27;
    resp[41] = 0x0f;
    resp[42] = 0x01;
    resp[43] = 0xbb;
    resp[54] = 0xff;
    resp[55] = 0xff;
    resp[56] = 8;
    resp[57] = 8;
    resp[58] = 8;
    resp[59] = 8;
    char ip[16];
    uint16_t port = 0;
    uint32_t life = 0;
    Expect(net::ParsePcpMap(resp, 64, nonce, 9999, ip, 16, &port, &life), "pcp parse tail");
    Expect(std::strcmp(ip, "8.8.8.8") == 0 && port == 443 && life == 3600, "pcp mapped");
    Expect(net::PublicIPv4(ip), "pcp public");

    resp[56] = 100;
    resp[57] = 64;
    resp[58] = 0;
    resp[59] = 1;
    Expect(net::ParsePcpMap(resp, 60, nonce, 9999, ip, 16, &port, &life), "pcp parse cgnat");
    Expect(std::strcmp(ip, "100.64.0.1") == 0 && !net::PublicIPv4(ip), "pcp cgnat refused");
    resp[56] = 192;
    resp[57] = 168;
    resp[58] = 1;
    resp[59] = 1;
    Expect(net::ParsePcpMap(resp, 60, nonce, 9999, ip, 16, &port, &life), "pcp parse lan");
    Expect(!net::PublicIPv4(ip), "pcp lan refused");
    resp[56] = 127;
    resp[57] = 0;
    resp[58] = 0;
    resp[59] = 1;
    Expect(net::ParsePcpMap(resp, 60, nonce, 9999, ip, 16, &port, &life), "pcp parse loop");
    Expect(!net::PublicIPv4(ip), "pcp loop refused");

    resp[56] = 8;
    resp[57] = 8;
    resp[58] = 8;
    resp[59] = 8;
    resp[0] = 0;
    Expect(!net::ParsePcpMap(resp, 60, nonce, 9999, ip, 16, &port, &life), "pcp not nat-pmp");
    resp[0] = 2;
    resp[1] = 1;
    Expect(!net::ParsePcpMap(resp, 60, nonce, 9999, ip, 16, &port, &life), "pcp request bit");
    resp[1] = 0x81;
    resp[3] = 11;
    Expect(!net::ParsePcpMap(resp, 60, nonce, 9999, ip, 16, &port, &life), "pcp result");
    resp[3] = 0;
    resp[36] = 6;
    Expect(!net::ParsePcpMap(resp, 60, nonce, 9999, ip, 16, &port, &life), "pcp not udp");
    resp[36] = 17;
    nonce[0] ^= 0xff;
    Expect(!net::ParsePcpMap(resp, 60, nonce, 9999, ip, 16, &port, &life), "pcp bad nonce");
    nonce[0] ^= 0xff;
    Expect(!net::ParsePcpMap(resp, 59, nonce, 9999, ip, 16, &port, &life), "pcp short resp");
    Expect(!net::ParsePcpMap(resp, 60, nonce, 9998, ip, 16, &port, &life), "pcp internal mismatch");

    if (g_fails) {
        std::fprintf(stderr, "FAIL %d\n", g_fails);
        return 1;
    }
    std::printf("PASS port_map\n");
    return 0;
}
