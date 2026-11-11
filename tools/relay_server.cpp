// Local UDP relay. The client cannot pick the session.
// One session holds four seats. The default cap is one session.
// Type byte, then a 16 byte token where the packet has one:
//   1 Open (empty) -> 4 Accept + token, or 5 Reject
//   2 Join + token -> 6 Joined + seat, or 5 Reject
//   3 Data + token + payload -> payload only, to every other bound seat
//   7 Roster + count + seat bytes, sent to every bound seat
//   8 Start + token, host only -> 8 Start + mask, to every bound seat, or 5 Reject
// The mask bit 0 is seat 1. After Start, Join is Reject.
// A second endpoint for a seat is dropped.
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <random>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr uint8_t kOpen = 1;
constexpr uint8_t kJoin = 2;
constexpr uint8_t kData = 3;
constexpr uint8_t kAccept = 4;
constexpr uint8_t kReject = 5;
constexpr uint8_t kJoined = 6;
constexpr uint8_t kRoster = 7;
constexpr uint8_t kStart = 8;
constexpr int kToken = 16;
constexpr int kSeats = 4;

using Token = std::array<uint8_t, kToken>;
using Clock = std::chrono::steady_clock;

struct Session {
    sockaddr_in seat[kSeats]{};
    bool have[kSeats]{};
    int count = 0;
    bool started = false;
    uint8_t start_mask = 0;
    Clock::time_point opened{};
    Clock::time_point last{};
};

bool SameEndpoint(const sockaddr_in& a, const sockaddr_in& b) {
    return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}

void Reply(int sock, const sockaddr_in& to, uint8_t type, const uint8_t* extra, int n) {
    uint8_t buf[1 + kToken];
    buf[0] = type;
    if (n > 0 && extra) std::memcpy(buf + 1, extra, size_t(n));
    sendto(sock, buf, size_t(1 + n), 0, reinterpret_cast<const sockaddr*>(&to), sizeof to);
}

void SendRoster(int sock, const Session& session) {
    uint8_t buf[2 + kSeats];
    buf[0] = kRoster;
    buf[1] = uint8_t(session.count);
    int n = 2;
    for (int i = 0; i < kSeats; i++) {
        if (!session.have[i]) continue;
        buf[n++] = uint8_t(i + 1);
    }
    for (int i = 0; i < kSeats; i++) {
        if (!session.have[i]) continue;
        sendto(sock, buf, size_t(n), 0, reinterpret_cast<const sockaddr*>(&session.seat[i]), sizeof session.seat[i]);
    }
}

int FindSeat(const Session& session, const sockaddr_in& who) {
    for (int i = 0; i < kSeats; i++) {
        if (session.have[i] && SameEndpoint(session.seat[i], who)) return i;
    }
    return -1;
}

void Mint(Token& out) {
    std::random_device rd;
    for (int i = 0; i < 4; i++) {
        uint32_t w = rd();
        std::memcpy(out.data() + i * 4, &w, 4);
    }
}

void Sweep(std::map<Token, Session>& sessions, Clock::time_point now, std::chrono::seconds idle,
           std::chrono::seconds life) {
    for (auto it = sessions.begin(); it != sessions.end();) {
        const Session& s = it->second;
        if (now - s.last >= idle || now - s.opened >= life) it = sessions.erase(it);
        else ++it;
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: relay_server <port> [max_sessions] [idle_sec] [abs_sec]\n";
        return 1;
    }

    int port = std::stoi(argv[1]);
    int max_sessions = argc >= 3 ? std::stoi(argv[2]) : 1;
    int idle_sec = argc >= 4 ? std::stoi(argv[3]) : 15 * 60;
    int abs_sec = argc >= 5 ? std::stoi(argv[4]) : 3 * 60 * 60;
    if (max_sessions < 1) max_sessions = 1;
    if (idle_sec < 1) idle_sec = 1;
    if (abs_sec < 1) abs_sec = 1;
    const auto idle = std::chrono::seconds(idle_sec);
    const auto life = std::chrono::seconds(abs_sec);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(uint16_t(port));
    if (bind(sock, reinterpret_cast<sockaddr*>(&server_addr), sizeof server_addr) < 0) {
        perror("bind");
        return 1;
    }

    std::cout << "Relay listening on port " << port << " cap " << max_sessions << " idle " << idle_sec
              << "s life " << abs_sec << "s" << std::endl;

    std::map<Token, Session> sessions;
    uint8_t buffer[2048];
    while (true) {
        sockaddr_in client{};
        socklen_t addr_len = sizeof client;
        ssize_t len = recvfrom(sock, buffer, sizeof buffer, 0, reinterpret_cast<sockaddr*>(&client), &addr_len);
        if (len < 1) continue;
        const auto now = Clock::now();
        Sweep(sessions, now, idle, life);
        const uint8_t type = buffer[0];

        if (type == kOpen) {
            if (int(sessions.size()) >= max_sessions) {
                Reply(sock, client, kReject, nullptr, 0);
                continue;
            }
            Token token{};
            bool minted = false;
            for (int n = 0; n < 8; n++) {
                Mint(token);
                if (sessions.find(token) == sessions.end()) {
                    minted = true;
                    break;
                }
            }
            if (!minted) {
                Reply(sock, client, kReject, nullptr, 0);
                continue;
            }
            Session session;
            session.seat[0] = client;
            session.have[0] = true;
            session.count = 1;
            session.opened = now;
            session.last = now;
            sessions.emplace(token, session);
            Reply(sock, client, kAccept, token.data(), kToken);
            SendRoster(sock, sessions[token]);
            continue;
        }

        if (len < 1 + kToken) {
            Reply(sock, client, kReject, nullptr, 0);
            continue;
        }
        Token token{};
        std::memcpy(token.data(), buffer + 1, kToken);
        auto it = sessions.find(token);

        if (type == kJoin) {
            if (it == sessions.end() || it->second.started || it->second.count >= kSeats ||
                FindSeat(it->second, client) >= 0) {
                Reply(sock, client, kReject, nullptr, 0);
                continue;
            }
            int slot = -1;
            for (int i = 1; i < kSeats; i++) {
                if (!it->second.have[i]) {
                    slot = i;
                    break;
                }
            }
            if (slot < 0) {
                Reply(sock, client, kReject, nullptr, 0);
                continue;
            }
            it->second.seat[slot] = client;
            it->second.have[slot] = true;
            it->second.count++;
            it->second.last = now;
            uint8_t seat = uint8_t(slot + 1);
            Reply(sock, client, kJoined, &seat, 1);
            SendRoster(sock, it->second);
            continue;
        }

        if (type == kStart) {
            if (it == sessions.end()) {
                Reply(sock, client, kReject, nullptr, 0);
                continue;
            }
            int from = FindSeat(it->second, client);
            if (from != 0 || it->second.started) {
                Reply(sock, client, kReject, nullptr, 0);
                continue;
            }
            it->second.last = now;
            uint8_t mask = 0;
            for (int i = 0; i < kSeats; i++)
                if (it->second.have[i]) mask = uint8_t(mask | (1u << i));
            it->second.started = true;
            it->second.start_mask = mask;
            uint8_t buf[2] = {kStart, mask};
            for (int i = 0; i < kSeats; i++) {
                if (!it->second.have[i]) continue;
                sendto(sock, buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&it->second.seat[i]),
                       sizeof it->second.seat[i]);
            }
            continue;
        }

        if (type != kData || it == sessions.end()) continue;
        Session& session = it->second;
        int from = FindSeat(session, client);
        if (from < 0) continue;
        session.last = now;
        const uint8_t* payload = buffer + 1 + kToken;
        int payload_n = int(len) - (1 + kToken);
        if (payload_n <= 0) continue;
        for (int i = 0; i < kSeats; i++) {
            if (i == from || !session.have[i]) continue;
            if (session.started && (session.start_mask & (1u << i)) == 0) continue;
            sendto(sock, payload, size_t(payload_n), 0, reinterpret_cast<sockaddr*>(&session.seat[i]),
                   sizeof session.seat[i]);
        }
    }
}
