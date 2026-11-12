#include "relay_client.h"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
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
std::atomic<int> g_restart_ui{0};
char g_saved_code[8]{};
char g_saved_relay[80]{};
constexpr char kAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
// Empty until a public relay hostname is chosen. FUZION_RELAY=host:port overrides it.
constexpr const char* kDefaultRelay = "";

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
bool RelayClient::OpenMatch(const OpenInfo&, uint8_t*, char*) { return false; }
bool RelayClient::Join(const uint8_t*, int*, int) { return false; }
bool RelayClient::SendRestart() { return false; }
bool RelayClient::Resolve(const char*, uint8_t*) { return false; }
bool RelayClient::Heartbeat() { return false; }
int RelayClient::List(const char*, uint16_t, Game*, int) { return -1; }
bool RelayClient::SendStart() { return false; }
void RelayClient::Collect() {}
bool RelayClient::Send(const uint8_t*, int) { return false; }
bool RelayClient::Recv(std::vector<std::vector<uint8_t>>&) { return false; }

}  // namespace net

#else

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>

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
constexpr uint8_t kResolve = 9;
constexpr uint8_t kList = 10;
constexpr uint8_t kHeartbeat = 11;
constexpr uint8_t kRestart = 12;
constexpr int kBudgetMs = 2000;

bool FillAddr(const std::string& host, uint16_t port, sockaddr_in* out) {
    if (!out || port == 0 || host.empty()) return false;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    char port_buf[8];
    std::snprintf(port_buf, sizeof port_buf, "%u", unsigned(port));
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), port_buf, &hints, &res) != 0 || !res) return false;
    std::memcpy(out, res->ai_addr, sizeof *out);
    freeaddrinfo(res);
    return out->sin_port != 0;
}
}  // namespace

RelayClient::RelayClient(Config cfg) : port_(cfg.relay_port), host_(std::move(cfg.relay_host)) {
    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) return;
    if (!SetSockAddr(host_, port_)) {
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
    return FillAddr(host, port, &relay_addr_);
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
        if (buf[0] == kRestart) {
            TakeRestart();
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
    OpenInfo info;
    info.listed = false;
    char code[kCodeLen + 1];
    return OpenMatch(info, token_out, code);
}

bool RelayClient::OpenMatch(const OpenInfo& info, uint8_t token_out[kTokenLen], char code_out[kCodeLen + 1]) {
    int name_n = 0;
    while (name_n < kNameLen && info.name[name_n]) name_n++;
    uint8_t pkt[1 + 1 + 1 + kNameLen];
    pkt[0] = kOpen;
    pkt[1] = info.listed ? 1 : 0;
    pkt[2] = uint8_t(name_n);
    if (name_n > 0) std::memcpy(pkt + 3, info.name, size_t(name_n));
    if (!RoundTrip(pkt, 3 + name_n, kAccept, token_, kTokenLen)) return false;
    if (token_out) std::memcpy(token_out, token_, kTokenLen);
    CodeOf(token_, code_);
    if (code_out) std::snprintf(code_out, kCodeLen + 1, "%s", code_);
    ready_ = true;
    return true;
}

bool RelayClient::Resolve(const char code[kCodeLen], uint8_t token_out[kTokenLen]) {
    if (!code || !token_out) return false;
    uint8_t pkt[1 + kCodeLen];
    pkt[0] = kResolve;
    std::memcpy(pkt + 1, code, kCodeLen);
    return RoundTrip(pkt, int(sizeof pkt), kAccept, token_out, kTokenLen);
}

bool RelayClient::Heartbeat() {
    if (!ready_ || sock_ < 0) return false;
    uint8_t pkt[1 + kTokenLen];
    pkt[0] = kHeartbeat;
    std::memcpy(pkt + 1, token_, kTokenLen);
    ssize_t sent = sendto(sock_, pkt, sizeof pkt, 0, reinterpret_cast<sockaddr*>(&relay_addr_), sizeof relay_addr_);
    return sent == ssize_t(sizeof pkt);
}

int RelayClient::List(const char* host, uint16_t port, Game* out, int cap) {
    if (!host || !host[0] || !out || cap <= 0 || port == 0) return -1;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;
    sockaddr_in dest{};
    if (!FillAddr(host, port, &dest)) {
        close(sock);
        return -1;
    }
    uint8_t pkt = kList;
    if (sendto(sock, &pkt, 1, 0, reinterpret_cast<sockaddr*>(&dest), sizeof dest) != 1) {
        close(sock);
        return -1;
    }
    pollfd pfd{};
    pfd.fd = sock;
    pfd.events = POLLIN;
    int rc = poll(&pfd, 1, kBudgetMs);
    if (rc <= 0) {
        close(sock);
        return -1;
    }
    uint8_t buf[1200];
    sockaddr_in from{};
    socklen_t flen = sizeof from;
    ssize_t got = recvfrom(sock, buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &flen);
    close(sock);
    if (got < 2 || buf[0] != kList) return -1;
    int count = buf[1];
    int off = 2;
    int n = 0;
    for (int i = 0; i < count && n < cap; i++) {
        if (off + kCodeLen + 1 > int(got)) break;
        int name_n = buf[off + kCodeLen];
        if (name_n > kNameLen || off + kCodeLen + 1 + name_n + 1 > int(got)) break;
        std::memcpy(out[n].code, buf + off, kCodeLen);
        out[n].code[kCodeLen] = '\0';
        off += kCodeLen + 1;
        if (name_n > 0) std::memcpy(out[n].name, buf + off, size_t(name_n));
        out[n].name[name_n] = '\0';
        off += name_n;
        out[n].seats = buf[off++];
        n++;
    }
    return n;
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

bool RelayClient::Join(const uint8_t token[kTokenLen], int* seat, int prefer) {
    if (!token || !seat) return false;
    uint8_t pkt[1 + kTokenLen + 1];
    pkt[0] = kJoin;
    std::memcpy(pkt + 1, token, kTokenLen);
    int n = 1 + kTokenLen;
    if (prefer >= 1 && prefer <= 4) pkt[n++] = uint8_t(prefer);
    uint8_t got_seat = 0;
    if (!RoundTrip(pkt, n, kJoined, &got_seat, 1)) return false;
    if (got_seat < 1 || got_seat > 4) return false;
    *seat = int(got_seat);
    std::memcpy(token_, token, kTokenLen);
    CodeOf(token_, code_);
    ready_ = true;
    return true;
}

void RelayClient::TakeRestart() {
    restart_ = true;
    g_restart_ui.store(1, std::memory_order_release);
}

bool RelayClient::SendRestart() {
    if (!ready_ || sock_ < 0) return false;
    uint8_t pkt[1 + kTokenLen];
    pkt[0] = kRestart;
    std::memcpy(pkt + 1, token_, kTokenLen);
    ssize_t sent = sendto(sock_, pkt, sizeof pkt, 0, reinterpret_cast<sockaddr*>(&relay_addr_), sizeof relay_addr_);
    return sent == ssize_t(sizeof pkt);
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
        if (buf[0] == kRestart) {
            TakeRestart();
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
        if (client) {
            std::snprintf(g_saved_code, sizeof g_saved_code, "%s", client->Code());
            std::snprintf(g_saved_relay, sizeof g_saved_relay, "%s:%u", client->Host().c_str(),
                          unsigned(client->Port()));
        }
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

bool RelayHeartbeat() {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_seat != 1 || !g_client) return false;
    return g_client->Heartbeat();
}

bool RelayWantsRestart() {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_client) {
        g_client->Collect();
        if (g_client->Restart()) g_restart_ui.store(1, std::memory_order_release);
    }
    return g_restart_ui.load(std::memory_order_acquire) != 0;
}

bool RelayRejoinInfo(char* code, int code_n, char* relay, int relay_n) {
    if (!code || code_n < 2 || !relay || relay_n < 2) return false;
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_saved_code[0]) return false;
    std::snprintf(code, size_t(code_n), "%s", g_saved_code);
    std::snprintf(relay, size_t(relay_n), "%s", g_saved_relay);
    return true;
}

int RelaySeat() {
    std::lock_guard<std::mutex> lock(g_mu);
    return g_seat;
}

bool RelayRequestRestart() {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_seat != 1 || !g_client) return false;
    return g_client->SendRestart();
}

void RelayClient::CodeOf(const uint8_t token[kTokenLen], char out[kCodeLen + 1]) {
    if (!out) return;
    if (!token) {
        out[0] = '\0';
        return;
    }
    uint32_t v = (uint32_t(token[0]) << 24) | (uint32_t(token[1]) << 16) | (uint32_t(token[2]) << 8) | uint32_t(token[3]);
    v >>= 2;
    for (int i = 0; i < kCodeLen; i++) {
        out[i] = kAlphabet[v & 31];
        v >>= 5;
    }
    out[kCodeLen] = '\0';
}

bool RelayClient::DefaultRelay(char* host, int host_n, uint16_t* port) {
    if (!host || host_n < 2 || !port) return false;
    const char* env = std::getenv("FUZION_RELAY");
    const char* text = (env && env[0]) ? env : kDefaultRelay;
    if (!text || !text[0]) return false;
    const char* colon = std::strrchr(text, ':');
    if (!colon || colon == text) return false;
    int n = int(colon - text);
    if (n <= 0 || n >= host_n) return false;
    std::memcpy(host, text, size_t(n));
    host[n] = '\0';
    int p = std::atoi(colon + 1);
    if (p <= 0 || p > 65535) return false;
    *port = uint16_t(p);
    return true;
}

}  // namespace net
