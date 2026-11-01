// Delay lockstep. Packet layout, little-endian:
//   4 bytes magic "FFL1", 1 byte type.
//   type 1 hello: u32 seed, u16 delay, u8 seat, u32 width, u32 height, u8 ultrawide.
//   type 2 state: u8 nIn, nIn times (u32 frame, u8 connected, u16 buttons,
//   8 analog bytes, i16 lx ly rx ry), then u8 nHash, nHash times (u32 frame, u64 total).
#include "net/lockstep.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace net {
namespace {

constexpr uint8_t kMagic[4] = {'F', 'F', 'L', '1'};
constexpr uint8_t kHello = 1;
constexpr uint8_t kState = 2;
constexpr int kWindow = 24;
constexpr uint64_t kMaxFrame = 1000000;

void PutU8(std::vector<uint8_t>& o, uint8_t v) { o.push_back(v); }

void PutU16(std::vector<uint8_t>& o, uint16_t v) {
    o.push_back(uint8_t(v));
    o.push_back(uint8_t(v >> 8));
}

void PutU32(std::vector<uint8_t>& o, uint32_t v) {
    o.push_back(uint8_t(v));
    o.push_back(uint8_t(v >> 8));
    o.push_back(uint8_t(v >> 16));
    o.push_back(uint8_t(v >> 24));
}

void PutU64(std::vector<uint8_t>& o, uint64_t v) {
    for (int i = 0; i < 8; i++) o.push_back(uint8_t(v >> (8 * i)));
}

void PutReport(std::vector<uint8_t>& o, const Report& r) {
    PutU8(o, r.connected ? 1 : 0);
    PutU16(o, r.buttons);
    for (int i = 0; i < 8; i++) PutU8(o, r.analog[i]);
    PutU16(o, uint16_t(r.lx));
    PutU16(o, uint16_t(r.ly));
    PutU16(o, uint16_t(r.rx));
    PutU16(o, uint16_t(r.ry));
}

struct Rd {
    const uint8_t* p;
    const uint8_t* end;
    bool Ok(int n) const { return p && end && end - p >= n; }
    uint8_t U8() {
        uint8_t v = p[0];
        p++;
        return v;
    }
    uint16_t U16() {
        uint16_t v = uint16_t(p[0] | (uint16_t(p[1]) << 8));
        p += 2;
        return v;
    }
    uint32_t U32() {
        uint32_t v = uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
        p += 4;
        return v;
    }
    uint64_t U64() {
        uint64_t v = 0;
        for (int i = 0; i < 8; i++) v |= uint64_t(p[i]) << (8 * i);
        p += 8;
        return v;
    }
    Report Rep() {
        Report r;
        r.connected = U8() ? 1 : 0;
        r.buttons = U16();
        for (int i = 0; i < 8; i++) r.analog[i] = U8();
        r.lx = int16_t(U16());
        r.ly = int16_t(U16());
        r.rx = int16_t(U16());
        r.ry = int16_t(U16());
        return r;
    }
};

#if defined(_WIN32)
struct WsaOnce {
    WsaOnce() {
        WSADATA w;
        WSAStartup(MAKEWORD(2, 2), &w);
    }
};
WsaOnce g_wsa;

bool WouldBlock() {
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAECONNRESET || e == WSAECONNREFUSED;
}

void CloseFd(SOCKET fd) {
    if (fd != INVALID_SOCKET) closesocket(fd);
}
#else
bool WouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNREFUSED; }

void CloseFd(int fd) {
    if (fd >= 0) close(fd);
}
#endif

bool ParseHp(const char* hp, sockaddr_in* out) {
    if (!hp || !out) return false;
    const char* colon = std::strrchr(hp, ':');
    if (!colon || colon == hp || !colon[1]) return false;
    std::string host(hp, colon);
    char* end = nullptr;
    long port = std::strtol(colon + 1, &end, 10);
    if (!end || *end || port < 0 || port > 65535) return false;
    std::memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    out->sin_port = htons(uint16_t(port));
    return inet_pton(AF_INET, host.c_str(), &out->sin_addr) == 1;
}

}  // namespace

struct Udp::Impl {
#if defined(_WIN32)
    SOCKET fd = INVALID_SOCKET;
#else
    int fd = -1;
#endif
    sockaddr_in dest{};
    int bound_port = 0;
    bool ok = false;
};

Udp::Udp(const char* bind_hp, const char* peer_hp) : impl_(new Impl) {
    sockaddr_in local{};
    if (!ParseHp(bind_hp, &local) || !ParseHp(peer_hp, &impl_->dest)) return;
#if defined(_WIN32)
    impl_->fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (impl_->fd == INVALID_SOCKET) return;
    u_long nb = 1;
    ioctlsocket(impl_->fd, FIONBIO, &nb);
#else
    impl_->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (impl_->fd < 0) return;
    int flags = fcntl(impl_->fd, F_GETFL, 0);
    if (flags >= 0) fcntl(impl_->fd, F_SETFL, flags | O_NONBLOCK);
#endif
    int reuse = 1;
    setsockopt(impl_->fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&reuse), sizeof reuse);
    if (bind(impl_->fd, reinterpret_cast<sockaddr*>(&local), sizeof local) != 0) {
        CloseFd(impl_->fd);
#if defined(_WIN32)
        impl_->fd = INVALID_SOCKET;
#else
        impl_->fd = -1;
#endif
        return;
    }
    sockaddr_in got{};
#if defined(_WIN32)
    int glen = sizeof got;
#else
    socklen_t glen = sizeof got;
#endif
    if (getsockname(impl_->fd, reinterpret_cast<sockaddr*>(&got), &glen) == 0) impl_->bound_port = ntohs(got.sin_port);
    impl_->ok = true;
}

Udp::~Udp() {
    if (!impl_) return;
    CloseFd(impl_->fd);
    delete impl_;
}

bool Udp::Ok() const { return impl_ && impl_->ok; }
int Udp::BoundPort() const { return impl_ ? impl_->bound_port : 0; }

bool Udp::Send(const uint8_t* data, int n) {
    if (!Ok() || !data || n <= 0) return false;
    sendto(impl_->fd, reinterpret_cast<const char*>(data), n, 0, reinterpret_cast<sockaddr*>(&impl_->dest),
           sizeof impl_->dest);
    return true;
}

bool Udp::Recv(std::vector<std::vector<uint8_t>>& out) {
    if (!Ok()) return false;
    for (;;) {
        uint8_t buf[2048];
        sockaddr_in from{};
#if defined(_WIN32)
        int flen = sizeof from;
        int n = recvfrom(impl_->fd, reinterpret_cast<char*>(buf), int(sizeof buf), 0, reinterpret_cast<sockaddr*>(&from),
                         &flen);
#else
        socklen_t flen = sizeof from;
        int n = int(recvfrom(impl_->fd, buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &flen));
#endif
        if (n < 0) return WouldBlock() || out.size();
        if (n == 0) return true;
        out.emplace_back(buf, buf + n);
    }
}

Session::Session(Config cfg) : cfg_(cfg) {}

void Session::SetPipe(std::unique_ptr<Datagram> pipe) { pipe_ = std::move(pipe); }

Result Session::FailNow(Fail f, uint64_t frame, uint64_t local, uint64_t remote) {
    Result r;
    r.fail = f;
    r.frame = frame;
    r.local_hash = local;
    r.remote_hash = remote;
    if (f == Fail::Desync) r.why = "hash mismatch";
    else if (f == Fail::Timeout) r.why = "timeout";
    else r.why = "hello rejected";
    return r;
}

Session::Slot& Session::At(uint64_t frame) {
    if (slots_.size() <= frame) slots_.resize(size_t(frame) + 1);
    return slots_[frame];
}

bool Session::SendRaw(const std::vector<uint8_t>& bytes) {
    if (!pipe_ || bytes.empty()) return false;
    return pipe_->Send(bytes.data(), int(bytes.size()));
}

void Session::SendHello() {
    std::vector<uint8_t> o;
    for (uint8_t b : kMagic) PutU8(o, b);
    PutU8(o, kHello);
    PutU32(o, cfg_.seed);
    PutU16(o, uint16_t(cfg_.delay));
    PutU8(o, uint8_t(cfg_.seat));
    PutU32(o, cfg_.width);
    PutU32(o, cfg_.height);
    PutU8(o, cfg_.ultrawide ? 1 : 0);
    SendRaw(o);
}

void Session::SendState() {
    uint64_t hi = 0;
    bool any = false;
    for (uint64_t f = 0; f < slots_.size(); f++) {
        if (!slots_[f].in && !slots_[f].hashed) continue;
        hi = f;
        any = true;
    }
    if (!any) return;
    uint64_t begin = hi > uint64_t(kWindow - 1) ? hi - uint64_t(kWindow - 1) : 0;
    std::vector<uint64_t> ins;
    std::vector<uint64_t> hashes;
    for (uint64_t f = begin; f <= hi; f++) {
        if (slots_[f].in) ins.push_back(f);
        if (slots_[f].hashed) hashes.push_back(f);
    }
    if (ins.empty() && hashes.empty()) return;
    std::vector<uint8_t> o;
    for (uint8_t b : kMagic) PutU8(o, b);
    PutU8(o, kState);
    PutU8(o, uint8_t(ins.size()));
    for (uint64_t f : ins) {
        PutU32(o, uint32_t(f));
        PutReport(o, slots_[f].report);
    }
    PutU8(o, uint8_t(hashes.size()));
    for (uint64_t f : hashes) {
        PutU32(o, uint32_t(f));
        PutU64(o, slots_[f].hash);
    }
    SendRaw(o);
}

void Session::Ingest(const uint8_t* p, int n) {
    if (!p || n < 5) return;
    if (std::memcmp(p, kMagic, 4) != 0) return;
    Rd rd{p + 5, p + n};
    if (p[4] == kHello) {
        if (!rd.Ok(4 + 2 + 1 + 4 + 4 + 1)) return;
        uint32_t seed = rd.U32();
        uint16_t delay = rd.U16();
        uint8_t seat = rd.U8();
        uint32_t width = rd.U32();
        uint32_t height = rd.U32();
        uint8_t wide = rd.U8();
        if (seed != cfg_.seed || int(delay) != cfg_.delay || width != cfg_.width || height != cfg_.height ||
            wide != (cfg_.ultrawide ? 1 : 0) || (seat != 1 && seat != 2) || int(seat) == cfg_.seat) {
            sticky_ = Fail::Hello;
            return;
        }
        hello_ok_ = true;
        return;
    }
    if (p[4] != kState || !hello_ok_) return;
    if (!rd.Ok(1)) return;
    int n_in = rd.U8();
    for (int i = 0; i < n_in; i++) {
        if (!rd.Ok(4 + 1 + 2 + 8 + 8)) return;
        uint32_t frame = rd.U32();
        Report rep = rd.Rep();
        if (frame >= kMaxFrame) return;
        Slot& s = At(frame);
        s.remote_in = true;
        s.remote = rep;
    }
    if (!rd.Ok(1)) return;
    int n_hash = rd.U8();
    for (int i = 0; i < n_hash; i++) {
        if (!rd.Ok(4 + 8)) return;
        uint32_t frame = rd.U32();
        uint64_t total = rd.U64();
        if (frame >= kMaxFrame) return;
        Slot& s = At(frame);
        s.remote_hashed = true;
        s.remote_hash = total;
        if (s.hashed && s.hash != total && sticky_ == Fail::None) {
            sticky_ = Fail::Desync;
            sticky_frame_ = frame;
            sticky_local_ = s.hash;
            sticky_remote_ = total;
        }
    }
}

void Session::Fill(uint64_t sample_frame, Report out[4]) {
    for (int i = 0; i < 4; i++) out[i] = {};
    if (int(sample_frame) < cfg_.delay) return;
    uint64_t src = sample_frame - uint64_t(cfg_.delay);
    if (src >= slots_.size()) return;
    const Slot& s = slots_[src];
    Report mine = s.in ? s.report : Report{};
    Report theirs = s.remote_in ? s.remote : Report{};
    if (cfg_.seat == 1) {
        out[0] = mine;
        out[1] = theirs;
    } else {
        out[0] = theirs;
        out[1] = mine;
    }
}

Result Session::Pump(int wait_ms, bool want_hello, int64_t need_remote, bool want_hashes, uint64_t through) {
    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();
    auto next_send = t0;
    for (;;) {
        if (sticky_ != Fail::None) return FailNow(sticky_, sticky_frame_, sticky_local_, sticky_remote_);
        auto now = clock::now();
        if (now >= next_send) {
            SendHello();
            SendState();
            next_send = now + std::chrono::milliseconds(10);
        }
        if (pipe_) {
            std::vector<std::vector<uint8_t>> pkts;
            pipe_->Recv(pkts);
            for (const std::vector<uint8_t>& pkt : pkts) Ingest(pkt.data(), int(pkt.size()));
        }
        if (sticky_ != Fail::None) return FailNow(sticky_, sticky_frame_, sticky_local_, sticky_remote_);
        bool hello_ready = !want_hello || hello_ok_;
        bool remote_ready = true;
        if (need_remote >= 0) {
            remote_ready = uint64_t(need_remote) < slots_.size() && slots_[size_t(need_remote)].remote_in;
        }
        bool hash_ready = true;
        if (want_hashes) {
            for (uint64_t f = 0; f < slots_.size() && f <= through; f++) {
                if (slots_[f].hashed && !slots_[f].remote_hashed) hash_ready = false;
            }
        }
        if (hello_ready && remote_ready && hash_ready) {
            Result r;
            return r;
        }
        if (wait_ms == 0) {
            Result r;
            return r;
        }
        if (now - t0 >= std::chrono::milliseconds(wait_ms)) {
            Result r = FailNow(Fail::Timeout, need_remote >= 0 ? uint64_t(need_remote) : 0, 0, 0);
            if (want_hello && !hello_ok_) r.why = "hello timeout";
            return r;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

Result Session::Hello() {
    if (sticky_ != Fail::None) return FailNow(sticky_, sticky_frame_, sticky_local_, sticky_remote_);
    if (hello_ok_) {
        Result r;
        return r;
    }
    if (cfg_.seat != 1 && cfg_.seat != 2) return FailNow(Fail::Hello, 0, 0, 0);
    if (cfg_.delay < 0 || cfg_.delay > 20) return FailNow(Fail::Hello, 0, 0, 0);
    if (!opened_) {
        if (!pipe_) {
            if (!cfg_.bind || !cfg_.peer) return FailNow(Fail::Hello, 0, 0, 0);
            auto udp = std::make_unique<Udp>(cfg_.bind, cfg_.peer);
            if (!udp->Ok()) return FailNow(Fail::Hello, 0, 0, 0);
            pipe_ = std::move(udp);
        }
        opened_ = true;
    }
    return Pump(cfg_.hello_ms, true, -1, false, 0);
}

Result Session::Agree(uint64_t sample_frame, const Report& local) {
    if (sticky_ != Fail::None) return FailNow(sticky_, sticky_frame_, sticky_local_, sticky_remote_);
    Result hello = Hello();
    if (hello.fail != Fail::None) return hello;
    if (sample_frame >= kMaxFrame) return FailNow(Fail::Hello, sample_frame, 0, 0);
    Slot& s = At(sample_frame);
    s.in = true;
    s.report = local;
    int64_t need = -1;
    if (int(sample_frame) >= cfg_.delay) need = int64_t(sample_frame) - cfg_.delay;
    Result r = Pump(cfg_.timeout_ms, false, need, false, 0);
    if (r.fail != Fail::None) return r;
    Fill(sample_frame, r.pad);
    return r;
}

Result Session::NoteHash(uint64_t frame, uint64_t total) {
    if (sticky_ != Fail::None) return FailNow(sticky_, sticky_frame_, sticky_local_, sticky_remote_);
    if (frame >= kMaxFrame) return FailNow(Fail::Hello, frame, 0, 0);
    Slot& s = At(frame);
    s.hashed = true;
    s.hash = total;
    if (s.remote_hashed && s.remote_hash != total) {
        sticky_ = Fail::Desync;
        sticky_frame_ = frame;
        sticky_local_ = total;
        sticky_remote_ = s.remote_hash;
        return FailNow(sticky_, sticky_frame_, sticky_local_, sticky_remote_);
    }
    return Pump(0, false, -1, false, 0);
}

Result Session::Drain(uint64_t through_frame) {
    if (sticky_ != Fail::None) return FailNow(sticky_, sticky_frame_, sticky_local_, sticky_remote_);
    return Pump(cfg_.timeout_ms, false, -1, true, through_frame);
}

}  // namespace net
