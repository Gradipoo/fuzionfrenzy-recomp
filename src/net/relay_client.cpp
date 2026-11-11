#include "relay_client.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <utility>

namespace net {
namespace {
std::mutex g_mu;
std::condition_variable g_cv;
int g_seat = 0;
int g_count = 0;
int g_mask = 0;
std::unique_ptr<RelayClient> g_client;
bool g_armed = false;
bool g_started = false;
std::atomic<int> g_started_ui{0};

void NoteStartLocked() {
    if (g_started || !g_client || !g_client->Started()) return;
    int mask = g_client->StartMask();
    if (g_seat < 1 || g_seat > 4) return;
    if ((mask & (1 << (g_seat - 1))) == 0) return;
    g_mask = mask;
    g_started = true;
    g_started_ui.store(1, std::memory_order_release);
}
}  // namespace
}  // namespace net

#if defined(_WIN32)

namespace net {

RelayClient::RelayClient(Config) {}
RelayClient::~RelayClient() {}
bool RelayClient::Open(uint8_t*) { return false; }
bool RelayClient::Join(const uint8_t*, int*) { return false; }
bool RelayClient::SendStart() { return false; }
void RelayClient::Collect() {}
bool RelayClient::Send(const uint8_t*, int) { return false; }
bool RelayClient::Recv(std::vector<std::vector<uint8_t>>&) { return false; }

}  // namespace net

#else

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace net {

namespace {
constexpr uint8_t kOpen = 1;
constexpr uint8_t kJoin = 2;
constexpr uint8_t kData = 3;
constexpr uint8_t kAccept = 4;
constexpr uint8_t kReject = 5;
constexpr uint8_t kJoined = 6;
constexpr uint8_t kRoster = 7;
constexpr uint8_t kStart = 8;
constexpr int kBudgetMs = 2000;
}  // namespace

RelayClient::RelayClient(Config cfg) {
    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) return;
    if (!SetSockAddr(cfg.relay_host, cfg.relay_port)) {
        close(sock_);
        sock_ = -1;
        return;
    }
    int flags = fcntl(sock_, F_GETFL, 0);
    if (flags >= 0) fcntl(sock_, F_SETFL, flags | O_NONBLOCK);
}

RelayClient::~RelayClient() {
    if (sock_ >= 0) close(sock_);
}

bool RelayClient::SetSockAddr(const std::string& host, uint16_t port) {
    std::memset(&relay_addr_, 0, sizeof relay_addr_);
    relay_addr_.sin_family = AF_INET;
    relay_addr_.sin_port = htons(port);
    return inet_pton(AF_INET, host.c_str(), &relay_addr_.sin_addr) == 1;
}

bool RelayClient::RoundTrip(const uint8_t* pkt, int n, uint8_t expect, uint8_t* extra, int extra_n) {
    fail_ = 1;
    if (sock_ < 0 || !pkt || n <= 0) return false;
    if (sendto(sock_, pkt, size_t(n), 0, reinterpret_cast<sockaddr*>(&relay_addr_), sizeof relay_addr_) != n)
        return false;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(kBudgetMs);
    uint8_t buf[64];
    while (std::chrono::steady_clock::now() < end) {
        int left = int(std::chrono::duration_cast<std::chrono::milliseconds>(end - std::chrono::steady_clock::now())
                           .count());
        if (left < 0) left = 0;
        pollfd pfd{};
        pfd.fd = sock_;
        pfd.events = POLLIN;
        int rc = poll(&pfd, 1, left);
        if (rc < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (rc == 0) return false;
        sockaddr_in from{};
        socklen_t flen = sizeof from;
        ssize_t got = recvfrom(sock_, buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &flen);
        if (got < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
            return false;
        }
        if (got < 1) continue;
        if (buf[0] == kRoster) {
            TakeRoster(buf, int(got));
            continue;
        }
        if (buf[0] == kStart) {
            TakeStart(buf, int(got));
            continue;
        }
        if (buf[0] == kReject) {
            fail_ = 2;
            return false;
        }
        if (buf[0] != expect) {
            fail_ = 2;
            return false;
        }
        if (extra_n > 0) {
            if (got != 1 + extra_n || !extra) return false;
            std::memcpy(extra, buf + 1, size_t(extra_n));
        }
        fail_ = 0;
        return true;
    }
    return false;
}

bool RelayClient::Open(uint8_t token_out[kTokenLen]) {
    uint8_t pkt = kOpen;
    if (!RoundTrip(&pkt, 1, kAccept, token_, kTokenLen)) return false;
    if (token_out) std::memcpy(token_out, token_, kTokenLen);
    ready_ = true;
    return true;
}

void RelayClient::TakeRoster(const uint8_t* buf, int n) {
    if (!buf || n < 2) return;
    int count = buf[1];
    if (count < 1 || count > 4 || n != 2 + count) return;
    roster_ = count;
}

void RelayClient::TakeStart(const uint8_t* buf, int n) {
    if (!buf || n != 2) return;
    int mask = buf[1] & 0x0F;
    if (mask == 0) return;
    start_mask_ = mask;
    started_ = true;
}

bool RelayClient::SendStart() {
    if (!ready_ || sock_ < 0) return false;
    uint8_t pkt[1 + kTokenLen];
    pkt[0] = kStart;
    std::memcpy(pkt + 1, token_, kTokenLen);
    ssize_t sent = sendto(sock_, pkt, sizeof pkt, 0, reinterpret_cast<sockaddr*>(&relay_addr_), sizeof relay_addr_);
    return sent == ssize_t(sizeof pkt);
}

bool RelayClient::Join(const uint8_t token[kTokenLen], int* seat) {
    if (!token || !seat) return false;
    uint8_t pkt[1 + kTokenLen];
    pkt[0] = kJoin;
    std::memcpy(pkt + 1, token, kTokenLen);
    uint8_t got_seat = 0;
    if (!RoundTrip(pkt, int(sizeof pkt), kJoined, &got_seat, 1)) return false;
    if (got_seat < 2 || got_seat > 4) return false;
    *seat = int(got_seat);
    std::memcpy(token_, token, kTokenLen);
    ready_ = true;
    return true;
}

void RelayClient::Collect() {
    if (sock_ < 0) return;
    for (;;) {
        uint8_t buf[2048];
        sockaddr_in from{};
        socklen_t flen = sizeof from;
        ssize_t n = recvfrom(sock_, buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &flen);
        if (n < 0) return;
        if (n == 0) continue;
        if (buf[0] == kRoster) {
            TakeRoster(buf, int(n));
            continue;
        }
        if (buf[0] == kStart) {
            TakeStart(buf, int(n));
            continue;
        }
        held_.emplace_back(buf, buf + n);
    }
}

bool RelayClient::Send(const uint8_t* data, int n) {
    if (!ready_ || sock_ < 0 || n < 0 || n > 2000) return false;
    if (n > 0 && !data) return false;
    uint8_t pkt[1 + kTokenLen + 2000];
    pkt[0] = kData;
    std::memcpy(pkt + 1, token_, kTokenLen);
    if (n > 0) std::memcpy(pkt + 1 + kTokenLen, data, size_t(n));
    int total = 1 + kTokenLen + n;
    ssize_t sent = sendto(sock_, pkt, size_t(total), 0, reinterpret_cast<sockaddr*>(&relay_addr_), sizeof relay_addr_);
    return sent == total;
}

bool RelayClient::Recv(std::vector<std::vector<uint8_t>>& out) {
    Collect();
    if (held_.empty()) return false;
    out.insert(out.end(), held_.begin(), held_.end());
    held_.clear();
    return true;
}

}  // namespace net

#endif

namespace net {

void ArmRelay(int seat, std::unique_ptr<RelayClient> client) {
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_seat = seat;
        g_client = std::move(client);
        g_armed = g_client != nullptr;
        g_started = false;
        g_mask = 0;
        g_started_ui.store(0, std::memory_order_release);
        NoteStartLocked();
    }
    g_cv.notify_all();
}

bool WaitRelay(int* seat, std::unique_ptr<RelayClient>* client, int timeout_ms) {
    if (!seat || !client) return false;
    std::unique_lock<std::mutex> lock(g_mu);
    if (timeout_ms < 0) timeout_ms = 0;
    if (!g_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [] { return g_armed; })) return false;
    *seat = g_seat;
    *client = std::move(g_client);
    g_armed = false;
    return *client != nullptr;
}

int RelayCount() {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_client) {
        g_client->Collect();
        g_count = g_client->Roster();
        NoteStartLocked();
    }
    return g_count;
}

bool RelayReady() {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_started) return true;
    if (!g_client) return false;
    g_client->Collect();
    g_count = g_client->Roster();
    NoteStartLocked();
    return g_started;
}

int RelayMask() {
    std::lock_guard<std::mutex> lock(g_mu);
    return g_mask;
}

bool RelayStarted() { return g_started_ui.load(std::memory_order_acquire) != 0; }

bool RelayRequestStart() {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_seat != 1 || !g_client) return false;
    return g_client->SendStart();
}

}  // namespace net
