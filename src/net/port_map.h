#pragma once
#include <cstdint>

namespace net {

struct HostedRelay {
    uint16_t local_port = 0;
    uint16_t public_port = 0;
    char public_ip[16]{};
};

// True for a public IPv4 address. Refuses loopback, 0/8, 10/8, 172.16/12,
// 192.168/16, 100.64/10, link-local, and multicast.
bool PublicIPv4(const char* text);

// 60 byte PCP MAP request (RFC 6887, version 2). Lifetime 0 deletes that map.
// client_ip is this host's IPv4 address toward the gateway. external_port 0
// means no suggested port.
bool BuildPcpMap(uint8_t* out, int out_n, const uint8_t nonce[12], const char* client_ip, uint16_t internal_port,
                 uint16_t external_port, uint32_t lifetime);

// PCP MAP response. Fills the mapped port and external address. The caller
// still rejects a private, loopback, or CGNAT address. n may be longer than
// 60 when the router appends options.
bool ParsePcpMap(const uint8_t* pkt, int n, const uint8_t nonce[12], uint16_t internal_port, char* external_ip,
                 int ip_n, uint16_t* external_port, uint32_t* lifetime);

// Spawn relay_server from exe_dir, map its UDP port, and leave both running.
// On failure the child is stopped and error (when set) is a panel sentence.
bool StartHostedRelay(const char* exe_dir, HostedRelay* out, char* error, int error_n);

// Drop the mapping and the relay this process owns. Safe when it owns neither.
void StopHostedRelay();

// After execv, the new image still owns the child. Refresh the lease and
// register cleanup. Call once at startup.
void InstallHostedRelay();

// relay_guard. Watches the host pid, then removes the mapping and the relay.
int GuardMain(int argc, char** argv);

}  // namespace net
