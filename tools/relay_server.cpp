// UDP relay. The client cannot pick the session.
// One session holds four seats. The default cap is one session.
// Type byte, then a body:
//   1 Open. Empty body: private, no name. Or flags, name length, name
//        (16 bytes max). Flag bit 0 lists the game.
//        -> 4 Accept + token, or 5 Reject
//   2 Join + token -> 6 Joined + seat, or 5 Reject
//   3 Data + token + payload -> payload only, to every other bound seat
//   7 Roster + count + seat bytes, sent to every bound seat
//   8 Start + token, host only -> 8 Start + mask, to every bound seat, or 5 Reject
//   9 Resolve + 6 char code -> 4 Accept + token, or 5 Reject
//  10 List -> 10 List + count + rows. A row is the code, the name length,
//        the name, and the seat count.
//  11 Heartbeat + token, host only. Refreshes the list time.
//  12 Restart + token, host only. Seats drop and rejoin. The same count
//      starts the match. Join may add a preferred seat byte.
// The mask bit 0 is seat 1. After Start, Join is Reject.
// A second endpoint for a seat is dropped.
// CodeOf matches src/net/relay_client.cpp.
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
constexpr uint8_t kResolve = 9;
constexpr uint8_t kList = 10;
constexpr uint8_t kHeartbeat = 11;
constexpr uint8_t kRestart = 12;
constexpr int kToken = 16;
constexpr int kSeats = 4;
constexpr int kCodeLen = 6;
constexpr int kNameLen = 16;
constexpr int kPerAddr = 4;
constexpr char kAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

using Token = std::array<uint8_t, kToken>;
using Clock = std::chrono::steady_clock;

struct Session {
    sockaddr_in seat[kSeats]{};
    bool have[kSeats]{};
    int count = 0;
    bool started = false;
    bool listed = false;
    bool restarting = false;
    int expect = 0;
    uint8_t start_mask = 0;
    char code[kCodeLen + 1]{};
    char name[kNameLen + 1]{};
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

void CodeOf(const Token& token, char out[kCodeLen + 1]) {
    uint32_t v = (uint32_t(token[0]) << 24) | (uint32_t(token[1]) << 16) | (uint32_t(token[2]) << 8) | uint32_t(token[3]);
    v >>= 2;
    for (int i = 0; i < kCodeLen; i++) {
        out[i] = kAlphabet[v & 31];
        v >>= 5;
    }
    out[kCodeLen] = '\0';
}

bool CodeUsed(const std::map<Token, Session>& sessions, const char* code) {
    for (const auto& it : sessions) {
        if (std::memcmp(it.second.code, code, kCodeLen) == 0) return true;
    }
    return false;
}

int CountAddr(const std::map<Token, Session>& sessions, const sockaddr_in& who) {
    int n = 0;
    for (const auto& it : sessions) {
        if (it.second.have[0] && it.second.seat[0].sin_addr.s_addr == who.sin_addr.s_addr) n++;
    }
    return n;
}

const Session* FindCode(const std::map<Token, Session>& sessions, const char* code, Token* token) {
    for (const auto& it : sessions) {
        if (std::memcmp(it.second.code, code, kCodeLen) != 0) continue;
        if (token) *token = it.first;
        return &it.second;
    }
    return nullptr;
}

void Sweep(std::map<Token, Session>& sessions, Clock::time_point now, std::chrono::seconds idle,
           std::chrono::seconds life) {
    for (auto it = sessions.begin(); it != sessions.end();) {
        const Session& s = it->second;
        if (now - s.last >= idle || now - s.opened >= life) it = sessions.erase(it);
        else ++it;
    }
}

void SendList(int sock, const sockaddr_in& to, const std::map<Token, Session>& sessions, Clock::time_point now) {
    uint8_t buf[1200];
    buf[0] = kList;
    int n = 2;
    int count = 0;
    const auto fresh = std::chrono::seconds(20);
    for (const auto& it : sessions) {
        const Session& s = it.second;
        if (!s.listed || s.started || now - s.last >= fresh) continue;
        int name_n = int(std::strlen(s.name));
        int row = kCodeLen + 1 + name_n + 1;
        if (n + row > int(sizeof buf) || count >= 32) break;
        std::memcpy(buf + n, s.code, kCodeLen);
        n += kCodeLen;
        buf[n++] = uint8_t(name_n);
        if (name_n > 0) std::memcpy(buf + n, s.name, size_t(name_n));
        n += name_n;
        buf[n++] = uint8_t(s.count);
        count++;
    }
    buf[1] = uint8_t(count);
    sendto(sock, buf, size_t(n), 0, reinterpret_cast<const sockaddr*>(&to), sizeof to);
}

bool TakeName(const uint8_t* buffer, int len, Session& session) {
    if (len == 1) {
        session.listed = false;
        session.name[0] = '\0';
        return true;
    }
    if (len < 3) return false;
    int name_n = buffer[2];
    if (name_n < 0 || name_n > kNameLen || len < 3 + name_n) return false;
    session.listed = (buffer[1] & 1) != 0;
    for (int i = 0; i < name_n; i++) {
        unsigned char c = buffer[3 + i];
        session.name[i] = (c >= 32 && c < 127) ? char(c) : '?';
    }
    session.name[name_n] = '\0';
    return true;
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

    // Port 0 asks the stack for a free port. Any other argument stays what the caller passed.
    if (port == 0) {
        sockaddr_in bound{};
        socklen_t bound_len = sizeof bound;
        if (getsockname(sock, reinterpret_cast<sockaddr*>(&bound), &bound_len) != 0) {
            perror("getsockname");
            return 1;
        }
        port = ntohs(bound.sin_port);
        if (port <= 0 || port > 65535) {
            std::cerr << "relay: bound port is not usable\n";
            return 1;
        }
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

        if (type == kList) {
            SendList(sock, client, sessions, now);
            continue;
        }

        if (type == kOpen) {
            if (int(sessions.size()) >= max_sessions || CountAddr(sessions, client) >= kPerAddr) {
                Reply(sock, client, kReject, nullptr, 0);
                continue;
            }
            Session session;
            if (!TakeName(buffer, int(len), session)) {
                Reply(sock, client, kReject, nullptr, 0);
                continue;
            }
            Token token{};
            bool minted = false;
            for (int n = 0; n < 8; n++) {
                Mint(token);
                CodeOf(token, session.code);
                if (sessions.find(token) == sessions.end() && !CodeUsed(sessions, session.code)) {
                    minted = true;
                    break;
                }
            }
            if (!minted) {
                Reply(sock, client, kReject, nullptr, 0);
                continue;
            }
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

        if (type == kResolve) {
            if (len != 1 + kCodeLen) {
                Reply(sock, client, kReject, nullptr, 0);
                continue;
            }
            char code[kCodeLen + 1];
            std::memcpy(code, buffer + 1, kCodeLen);
            code[kCodeLen] = '\0';
            Token token{};
            if (!FindCode(sessions, code, &token)) {
                Reply(sock, client, kReject, nullptr, 0);
                continue;
            }
            Reply(sock, client, kAccept, token.data(), kToken);
            continue;
        }

        if (len < 1 + kToken) {
            Reply(sock, client, kReject, nullptr, 0);
            continue;
        }
        Token token{};
        std::memcpy(token.data(), buffer + 1, kToken);
        auto it = sessions.find(token);

        if (type == kHeartbeat) {
            if (it == sessions.end() || FindSeat(it->second, client) != 0) continue;
            it->second.last = now;
            continue;
        }

        if (type == kRestart) {
            if (it == sessions.end() || FindSeat(it->second, client) != 0 || it->second.started ||
                it->second.restarting) {
                Reply(sock, client, kReject, nullptr, 0);
                continue;
            }
            sockaddr_in held[kSeats];
            int held_n = 0;
            int expect = it->second.count;
            for (int i = 0; i < kSeats; i++) {
                if (!it->second.have[i]) continue;
                held[held_n++] = it->second.seat[i];
                it->second.have[i] = false;
            }
            it->second.count = 0;
            it->second.expect = expect;
            it->second.restarting = true;
            it->second.last = now;
            uint8_t note = kRestart;
            for (int i = 0; i < held_n; i++)
                sendto(sock, &note, 1, 0, reinterpret_cast<sockaddr*>(&held[i]), sizeof held[i]);
            continue;
        }

        if (type == kJoin) {
            if (it == sessions.end() || it->second.started || it->second.count >= kSeats ||
                FindSeat(it->second, client) >= 0) {
                Reply(sock, client, kReject, nullptr, 0);
                continue;
            }
            if (it->second.restarting && it->second.count >= it->second.expect) {
                Reply(sock, client, kReject, nullptr, 0);
                continue;
            }
            int slot = -1;
            if (it->second.restarting && len >= 1 + kToken + 1) {
                int prefer = buffer[1 + kToken];
                if (prefer >= 1 && prefer <= kSeats && !it->second.have[prefer - 1]) slot = prefer - 1;
            }
            int begin = it->second.restarting ? 0 : 1;
            if (slot < 0) {
                for (int i = begin; i < kSeats; i++) {
                    if (!it->second.have[i]) {
                        slot = i;
                        break;
                    }
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
            if (it->second.restarting && it->second.count >= it->second.expect) {
                uint8_t mask = 0;
                for (int i = 0; i < kSeats; i++)
                    if (it->second.have[i]) mask = uint8_t(mask | (1u << i));
                it->second.started = true;
                it->second.restarting = false;
                it->second.start_mask = mask;
                uint8_t buf[2] = {kStart, mask};
                for (int i = 0; i < kSeats; i++) {
                    if (!it->second.have[i]) continue;
                    sendto(sock, buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&it->second.seat[i]),
                           sizeof it->second.seat[i]);
                }
            }
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
