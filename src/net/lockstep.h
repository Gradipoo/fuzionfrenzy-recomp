#pragma once
// Delay lockstep over datagrams. Off unless a caller builds a Session.
// Frame N runs the inputs sampled at frame N - delay. Ports 3 and 4 stay empty.
#include <cstdint>
#include <memory>
#include <vector>

namespace net {

struct Report {
    uint8_t connected = 0;
    uint16_t buttons = 0;
    uint8_t analog[8] = {};
    int16_t lx = 0, ly = 0, rx = 0, ry = 0;
};

enum class Fail : int { None = 0, Desync = 1, Timeout = 2, Hello = 3 };

struct Config {
    const char* bind = nullptr;  // "127.0.0.1:27015" or "0.0.0.0:27015"
    const char* peer = nullptr;
    int seat = 1;  // 1 or 2. Seat 1 is port 1. Seat 2 is port 2.
    int delay = 2;
    int timeout_ms = 5000;
    int hello_ms = 180000;
    uint32_t seed = 1;
    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t ultrawide = 1;
};

struct Result {
    Fail fail = Fail::None;
    uint64_t frame = 0;
    uint64_t local_hash = 0;
    uint64_t remote_hash = 0;
    const char* why = nullptr;
    Report pad[4] = {};
};

// One datagram in, one datagram out. The session owns retries.
class Datagram {
public:
    virtual ~Datagram() = default;
    virtual bool Send(const uint8_t* data, int n) = 0;
    virtual bool Recv(std::vector<std::vector<uint8_t>>& out) = 0;
};

class Udp final : public Datagram {
public:
    Udp(const char* bind_hp, const char* peer_hp);
    ~Udp() override;
    Udp(const Udp&) = delete;
    Udp& operator=(const Udp&) = delete;
    bool Ok() const;
    int BoundPort() const;
    bool Send(const uint8_t* data, int n) override;
    bool Recv(std::vector<std::vector<uint8_t>>& out) override;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

class Session {
public:
    explicit Session(Config cfg);
    // Test double. Hello opens a UDP socket when this was not called.
    void SetPipe(std::unique_ptr<Datagram> pipe);
    // Exchange the hello. Agree calls this.
    Result Hello();
    // `local` was sampled at `sample_frame`. The pads are what sim frame
    // `sample_frame` runs. Blocks until the remote sample for that frame
    // is in, or the timeout fires.
    Result Agree(uint64_t sample_frame, const Report& local);
    // Remember our total for `frame` and compare any remote total we have.
    Result NoteHash(uint64_t frame, uint64_t total);
    // Block until the peer's totals through `through_frame` have arrived.
    Result Drain(uint64_t through_frame);

private:
    struct Slot {
        bool in = false;
        Report report;
        bool hashed = false;
        uint64_t hash = 0;
        bool remote_in = false;
        Report remote;
        bool remote_hashed = false;
        uint64_t remote_hash = 0;
    };

    Config cfg_;
    std::unique_ptr<Datagram> pipe_;
    bool hello_ok_ = false;
    bool opened_ = false;
    Fail sticky_ = Fail::None;
    uint64_t sticky_frame_ = 0;
    uint64_t sticky_local_ = 0;
    uint64_t sticky_remote_ = 0;
    std::vector<Slot> slots_;

    Result FailNow(Fail f, uint64_t frame, uint64_t local, uint64_t remote);
    Slot& At(uint64_t frame);
    bool SendRaw(const std::vector<uint8_t>& bytes);
    void SendHello();
    void SendState();
    void Ingest(const uint8_t* p, int n);
    Result Pump(int wait_ms, bool want_hello, int64_t need_remote, bool want_hashes, uint64_t through);
    void Fill(uint64_t sample_frame, Report out[4]);
};

}  // namespace net
