#pragma once
// Relay datagram for delay lockstep. The server mints the 16 byte token.
// Open and Join block for about 2 seconds. Call them from a worker, not Draw.
#include "lockstep.h"

#include <cstdint>
#include <memory>
#include <string>
#ifndef _WIN32
#include <netinet/in.h>
#endif

namespace net {

class RelayClient final : public Datagram {
public:
    static constexpr int kTokenLen = 16;
    struct Config {
        std::string relay_host;
        uint16_t relay_port = 0;
    };
    explicit RelayClient(Config cfg);
    ~RelayClient() override;
    RelayClient(const RelayClient&) = delete;
    RelayClient& operator=(const RelayClient&) = delete;
    bool Open(uint8_t token_out[kTokenLen]);
    // 1 nothing answered. 2 the relay said no. 0 after a success.
    int LastFail() const { return fail_; }
    // Seat is 2, 3, or 4. The server assigns it.
    bool Join(const uint8_t token[kTokenLen], int* seat);
    // Host only. One datagram. The notice comes back through Collect.
    bool SendStart();
    // Read the socket. Roster and start notices update the getters. Lockstep payloads are kept.
    void Collect();
    int Roster() const { return roster_; }
    bool Started() const { return started_; }
    // Bit 0 is seat 1. 0 before the start notice.
    int StartMask() const { return start_mask_; }
    bool Send(const uint8_t* data, int n) override;
    bool Recv(std::vector<std::vector<uint8_t>>& out) override;

private:
#ifndef _WIN32
    bool SetSockAddr(const std::string& host, uint16_t port);
    bool RoundTrip(const uint8_t* pkt, int n, uint8_t expect, uint8_t* extra, int extra_n);
    void TakeRoster(const uint8_t* buf, int n);
    void TakeStart(const uint8_t* buf, int n);
    int sock_ = -1;
    sockaddr_in relay_addr_{};
    uint8_t token_[kTokenLen]{};
    bool ready_ = false;
    std::vector<std::vector<uint8_t>> held_;
#endif
    int roster_ = 0;
    int fail_ = 1;
    int start_mask_ = 0;
    bool started_ = false;
};

// Host is seat 1. Join uses the seat the server assigned. The caller must not use the socket after this.
void ArmRelay(int seat, std::unique_ptr<RelayClient> client);
// Guest thread. Moves the armed client out, or returns false on timeout.
bool WaitRelay(int* seat, std::unique_ptr<RelayClient>* client, int timeout_ms);
// How many seats the armed client has heard. 0 before Host or Join.
int RelayCount();
// True once the start notice includes this seat. Stays true after WaitRelay takes the client.
bool RelayReady();
// Seats in the start notice. Bit 0 is seat 1. 0 before the notice.
int RelayMask();
// True after this process has seen the start notice.
bool RelayStarted();
// Host only. Sends Start on the armed client. False when this process is not seat 1.
bool RelayRequestStart();

}  // namespace net

namespace det {
bool LockstepEnabled();
bool LockstepPeerSet();
}  // namespace det
