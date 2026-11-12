#include "net/port_map.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace net {

static bool Ipv4Bytes(const char* text, uint8_t out[4]) {
    if (!text || !text[0] || !out) return false;
    unsigned a = 0, b = 0, c = 0, d = 0;
    char extra = 0;
    if (std::sscanf(text, "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) != 4) return false;
    if (a > 255 || b > 255 || c > 255 || d > 255) return false;
    out[0] = uint8_t(a);
    out[1] = uint8_t(b);
    out[2] = uint8_t(c);
    out[3] = uint8_t(d);
    return true;
}

bool PublicIPv4(const char* text) {
    uint8_t o[4];
    if (!Ipv4Bytes(text, o)) return false;
    if (o[0] == 0 || o[0] == 10 || o[0] == 127) return false;
    if (o[0] == 172 && o[1] >= 16 && o[1] <= 31) return false;
    if (o[0] == 192 && o[1] == 168) return false;
    if (o[0] == 100 && o[1] >= 64 && o[1] <= 127) return false;
    if (o[0] == 169 && o[1] == 254) return false;
    if (o[0] >= 224) return false;
    return true;
}

bool BuildPcpMap(uint8_t* out, int out_n, const uint8_t nonce[12], const char* client_ip, uint16_t internal_port,
                 uint16_t external_port, uint32_t lifetime) {
    if (!out || out_n < 60 || !nonce || internal_port == 0) return false;
    uint8_t client[4];
    if (!Ipv4Bytes(client_ip, client)) return false;
    std::memset(out, 0, 60);
    out[0] = 2;
    out[1] = 1;
    out[4] = uint8_t(lifetime >> 24);
    out[5] = uint8_t(lifetime >> 16);
    out[6] = uint8_t(lifetime >> 8);
    out[7] = uint8_t(lifetime);
    out[18] = 0xff;
    out[19] = 0xff;
    std::memcpy(out + 20, client, 4);
    std::memcpy(out + 24, nonce, 12);
    out[36] = 17;
    out[40] = uint8_t(internal_port >> 8);
    out[41] = uint8_t(internal_port);
    out[42] = uint8_t(external_port >> 8);
    out[43] = uint8_t(external_port);
    return true;
}

bool ParsePcpMap(const uint8_t* pkt, int n, const uint8_t nonce[12], uint16_t internal_port, char* external_ip, int ip_n,
                 uint16_t* external_port, uint32_t* lifetime) {
    if (!pkt || n < 60 || !nonce || !external_ip || ip_n < 16 || !external_port || !lifetime) return false;
    if (pkt[0] != 2 || pkt[1] != 0x81 || pkt[3] != 0) return false;
    if (std::memcmp(pkt + 24, nonce, 12) != 0) return false;
    if (pkt[36] != 17) return false;
    unsigned internal = (unsigned(pkt[40]) << 8) | unsigned(pkt[41]);
    if (internal != internal_port) return false;
    unsigned pub = (unsigned(pkt[42]) << 8) | unsigned(pkt[43]);
    if (pub == 0 || pub > 65535) return false;
    for (int i = 0; i < 10; i++)
        if (pkt[44 + i] != 0) return false;
    if (pkt[54] != 0xff || pkt[55] != 0xff) return false;
    *lifetime = (uint32_t(pkt[4]) << 24) | (uint32_t(pkt[5]) << 16) | (uint32_t(pkt[6]) << 8) | uint32_t(pkt[7]);
    *external_port = uint16_t(pub);
    std::snprintf(external_ip, size_t(ip_n), "%u.%u.%u.%u", pkt[56], pkt[57], pkt[58], pkt[59]);
    return true;
}

}  // namespace net

#if defined(_WIN32)

namespace net {

bool StartHostedRelay(const char*, HostedRelay*, char* error, int error_n) {
    if (error && error_n > 0) std::snprintf(error, size_t(error_n), "Internet play is not available.");
    return false;
}

void StopHostedRelay() {}
void InstallHostedRelay() {}
int GuardMain(int, char**) { return 1; }

}  // namespace net

#else

#include <atomic>
#include <cerrno>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <libproc.h>
#include <net/route.h>
#include <stdlib.h>
#include <sys/sysctl.h>
#else
#include <net/if.h>
#endif

extern char** environ;

namespace net {
namespace {

constexpr int kLease = 3600;
constexpr const char* kNoRelay = "Could not start the relay.";
constexpr const char* kNoPort = "The router did not open the port.";
constexpr const char* kNoInet = "Internet play is not available.";

struct MapState {
    int kind = 0;  // 1 UPnP, 2 NAT-PMP, 3 PCP
    int relay_pid = -1;
    int guard_pid = -1;
    uint16_t local_port = 0;
    uint16_t public_port = 0;
    char public_ip[16]{};
    char relay_start[80]{};
    char guard_start[80]{};
    char gateway[64]{};
    char client[64]{};
    char service[128]{};
    char control[512]{};
    uint8_t nonce[12]{};
};

std::mutex g_mu;
MapState g_state;
std::atomic<int> g_gen{0};

void SetErr(char* error, int error_n, const char* text) {
    if (error && error_n > 0) std::snprintf(error, size_t(error_n), "%s", text ? text : "");
}

bool ProcessStart(int pid, char* out, int n) {
    if (!out || n < 2 || pid <= 0) return false;
    out[0] = '\0';
#if defined(__APPLE__)
    proc_bsdinfo info{};
    int rc = proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, int(sizeof info));
    if (rc != int(sizeof info)) return false;
    std::snprintf(out, size_t(n), "%llu.%06llu", (unsigned long long)info.pbi_start_tvsec,
                  (unsigned long long)info.pbi_start_tvusec);
    return true;
#else
    char path[64];
    std::snprintf(path, sizeof path, "/proc/%d/stat", pid);
    FILE* f = std::fopen(path, "r");
    if (!f) return false;
    char buf[1024];
    if (!std::fgets(buf, int(sizeof buf), f)) {
        std::fclose(f);
        return false;
    }
    std::fclose(f);
    char* rp = std::strrchr(buf, ')');
    if (!rp || rp[1] == '\0') return false;
    char* p = rp + 2;
    for (int field = 3; field < 22; field++) {
        char* sp = std::strchr(p, ' ');
        if (!sp) return false;
        p = sp + 1;
    }
    unsigned long long ticks = std::strtoull(p, nullptr, 10);
    std::snprintf(out, size_t(n), "%llu", ticks);
    return true;
#endif
}

bool SameProcess(int pid, const char* start) {
    if (pid <= 1 || kill(pid, 0) != 0) return false;
    if (!start || !start[0]) return true;
    char now[80];
    if (!ProcessStart(pid, now, int(sizeof now))) return true;
    return std::strcmp(now, start) == 0;
}

void Reap(int pid) {
    for (int i = 0; i < 20; i++) {
        pid_t got = waitpid(pid, nullptr, WNOHANG);
        if (got == pid || (got < 0 && errno == ECHILD)) return;
        if (kill(pid, 0) != 0) {
            waitpid(pid, nullptr, WNOHANG);
            return;
        }
        poll(nullptr, 0, 50);
    }
}

void KillPid(int pid, const char* start, bool polite) {
    if (!SameProcess(pid, start)) return;
    kill(pid, polite ? SIGTERM : SIGKILL);
    if (polite) {
        for (int i = 0; i < 20; i++) {
            if (!SameProcess(pid, start)) break;
            poll(nullptr, 0, 50);
        }
        if (SameProcess(pid, start)) kill(pid, SIGKILL);
    }
    Reap(pid);
}

int Dial(const char* host, int port) {
    char port_s[8];
    std::snprintf(port_s, sizeof port_s, "%d", port);
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host, port_s, &hints, &res) != 0) return -1;
    int fd = -1;
    for (addrinfo* it = res; it; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) continue;
#if defined(SO_NOSIGPIPE)
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int rc = connect(fd, it->ai_addr, it->ai_addrlen);
        if (rc != 0 && errno != EINPROGRESS) {
            close(fd);
            fd = -1;
            continue;
        }
        pollfd pfd{fd, POLLOUT, 0};
        if (poll(&pfd, 1, 2000) <= 0) {
            close(fd);
            fd = -1;
            continue;
        }
        int err = 0;
        socklen_t el = sizeof err;
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el);
        if (err != 0) {
            close(fd);
            fd = -1;
            continue;
        }
        fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
        break;
    }
    freeaddrinfo(res);
    return fd;
}

bool SendAll(int fd, const char* data, int n) {
    int off = 0;
    while (off < n) {
        pollfd pfd{fd, POLLOUT, 0};
        if (poll(&pfd, 1, 3000) <= 0) return false;
#if defined(MSG_NOSIGNAL)
        ssize_t w = send(fd, data + off, size_t(n - off), MSG_NOSIGNAL);
#else
        ssize_t w = send(fd, data + off, size_t(n - off), 0);
#endif
        if (w < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            return false;
        }
        if (w == 0) return false;
        off += int(w);
    }
    return true;
}

bool RecvAppend(int fd, std::string* raw, int cap) {
    char buf[2048];
    pollfd pfd{fd, POLLIN, 0};
    int pr = poll(&pfd, 1, 3000);
    if (pr < 0 && errno == EINTR) return true;
    if (pr <= 0) return false;
    ssize_t n = recv(fd, buf, sizeof buf, 0);
    if (n < 0) return errno == EINTR || errno == EAGAIN;
    if (n == 0) return false;
    if (int(raw->size()) + int(n) > cap) return false;
    raw->append(buf, buf + n);
    return true;
}

void Decode(const char* in, int len, char* out, int n) {
    int j = 0;
    for (int i = 0; i < len && j + 1 < n;) {
        if (in[i] == '&') {
            if (i + 5 <= len && std::memcmp(in + i, "&amp;", 5) == 0) {
                out[j++] = '&';
                i += 5;
                continue;
            }
            if (i + 4 <= len && std::memcmp(in + i, "&lt;", 4) == 0) {
                out[j++] = '<';
                i += 4;
                continue;
            }
            if (i + 4 <= len && std::memcmp(in + i, "&gt;", 4) == 0) {
                out[j++] = '>';
                i += 4;
                continue;
            }
            if (i + 6 <= len && std::memcmp(in + i, "&quot;", 6) == 0) {
                out[j++] = '"';
                i += 6;
                continue;
            }
        }
        out[j++] = in[i++];
    }
    out[j] = '\0';
}

bool TagText(const std::string& xml, const char* tag, char* out, int n) {
    if (out && n > 0) out[0] = '\0';
    const char* p = xml.c_str();
    size_t tlen = std::strlen(tag);
    while ((p = std::strstr(p, tag)) != nullptr) {
        if (p > xml.c_str()) {
            char before = p[-1];
            if (before != '<' && before != ':') {
                p += tlen;
                continue;
            }
        }
        const char* gt = std::strchr(p, '>');
        if (!gt) return false;
        if (gt > p && gt[-1] == '/') {
            p = gt + 1;
            continue;
        }
        const char* start = gt + 1;
        const char* end = std::strstr(start, "</");
        if (!end) return false;
        while (start < end && (*start == ' ' || *start == '\n' || *start == '\r' || *start == '\t')) start++;
        while (end > start && (end[-1] == ' ' || end[-1] == '\n' || end[-1] == '\r' || end[-1] == '\t')) end--;
        Decode(start, int(end - start), out, n);
        return true;
    }
    return false;
}

struct Url {
    char host[256]{};
    int port = 80;
    char path[700]{};
};

bool ParseUrl(const char* text, Url* u) {
    std::memset(u, 0, sizeof *u);
    u->port = 80;
    if (!text || std::strncmp(text, "http://", 7) != 0) return false;
    const char* p = text + 7;
    const char* slash = std::strchr(p, '/');
    const char* host_end = slash ? slash : p + std::strlen(p);
    const char* colon = nullptr;
    for (const char* q = p; q < host_end; q++)
        if (*q == ':') colon = q;
    int host_n = int((colon ? colon : host_end) - p);
    if (host_n <= 0 || host_n >= int(sizeof u->host)) return false;
    std::memcpy(u->host, p, size_t(host_n));
    u->host[host_n] = '\0';
    if (colon) {
        int port = std::atoi(colon + 1);
        if (port <= 0 || port > 65535) return false;
        u->port = port;
    }
    if (!slash) std::snprintf(u->path, sizeof u->path, "/");
    else if (std::strlen(slash) >= sizeof u->path) return false;
    else std::snprintf(u->path, sizeof u->path, "%s", slash);
    return true;
}

bool JoinUrl(const char* base, const char* ref, char* out, int n) {
    if (!ref || !ref[0] || !out || n < 8) return false;
    if (std::strncmp(ref, "http://", 7) == 0) {
        if (int(std::strlen(ref)) >= n) return false;
        std::snprintf(out, size_t(n), "%s", ref);
        return true;
    }
    Url base_url;
    if (!ParseUrl(base, &base_url)) return false;
    if (ref[0] == '/') {
        return std::snprintf(out, size_t(n), "http://%s:%d%s", base_url.host, base_url.port, ref) < n;
    }
    const char* slash = std::strrchr(base + 7, '/');
    if (!slash) return std::snprintf(out, size_t(n), "http://%s:%d/%s", base_url.host, base_url.port, ref) < n;
    int keep = int(slash - base) + 1;
    if (keep + int(std::strlen(ref)) >= n) return false;
    std::memcpy(out, base, size_t(keep));
    std::snprintf(out + keep, size_t(n - keep), "%s", ref);
    return true;
}

bool Http(const Url& url, const char* body, const char* soap_action, std::string* raw) {
    int fd = Dial(url.host, url.port);
    if (fd < 0) return false;
    char head[1400];
    int body_n = body ? int(std::strlen(body)) : 0;
    int hn = 0;
    if (soap_action) {
        hn = std::snprintf(head, sizeof head,
                           "POST %s HTTP/1.1\r\nHost: %s:%d\r\nContent-Type: text/xml; charset=\"utf-8\"\r\n"
                           "SOAPAction: \"%s\"\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
                           url.path, url.host, url.port, soap_action, body_n);
    } else {
        hn = std::snprintf(head, sizeof head,
                           "GET %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n\r\n", url.path, url.host, url.port);
    }
    bool ok = hn > 0 && hn < int(sizeof head) && SendAll(fd, head, hn) && (body_n == 0 || SendAll(fd, body, body_n));
    raw->clear();
    if (ok) {
        while (raw->size() < 512 * 1024) {
            size_t before = raw->size();
            if (!RecvAppend(fd, raw, 512 * 1024)) break;
            if (raw->size() == before) break;
        }
    }
    close(fd);
    if (!ok || raw->empty()) return false;
    size_t line = raw->find("\r\n");
    std::string status = raw->substr(0, line == std::string::npos ? raw->size() : line);
    return status.find(" 200") != std::string::npos;
}

std::string BodyOf(const std::string& raw) {
    size_t at = raw.find("\r\n\r\n");
    if (at == std::string::npos) return {};
    std::string body = raw.substr(at + 4);
    std::string headers = raw.substr(0, at);
    if (headers.find("chunked") == std::string::npos && headers.find("Chunked") == std::string::npos) return body;
    std::string out;
    const char* p = body.c_str();
    while (*p) {
        char* end = nullptr;
        unsigned long n = std::strtoul(p, &end, 16);
        if (end == p) break;
        if (n == 0) break;
        const char* data = std::strstr(end, "\r\n");
        if (!data) break;
        data += 2;
        if (data + n > body.c_str() + body.size()) break;
        out.append(data, data + n);
        p = data + n;
        if (std::strncmp(p, "\r\n", 2) == 0) p += 2;
    }
    return out;
}

bool LocationOf(const char* msg, char* out, int n) {
    const char* p = msg;
    while (p && *p) {
        if (strncasecmp(p, "LOCATION:", 9) == 0) {
            p += 9;
            while (*p == ' ' || *p == '\t') p++;
            int i = 0;
            while (*p && *p != '\r' && *p != '\n' && i + 1 < n) out[i++] = *p++;
            out[i] = '\0';
            return i > 0;
        }
        const char* nl = std::strchr(p, '\n');
        if (!nl) break;
        p = nl + 1;
    }
    return false;
}

bool LocalIp(const char* peer, int port, char* out, int n) {
    if (!peer || !out || n < 8) return false;
    char port_s[8];
    std::snprintf(port_s, sizeof port_s, "%d", port > 0 ? port : 9);
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(peer, port_s, &hints, &res) != 0 || !res) return false;
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    bool ok = false;
    if (fd >= 0 && connect(fd, res->ai_addr, res->ai_addrlen) == 0) {
        sockaddr_in me{};
        socklen_t len = sizeof me;
        if (getsockname(fd, reinterpret_cast<sockaddr*>(&me), &len) == 0)
            ok = inet_ntop(AF_INET, &me.sin_addr, out, socklen_t(n)) != nullptr;
    }
    if (fd >= 0) close(fd);
    freeaddrinfo(res);
    return ok && out[0];
}

bool Soap(const char* control, const char* service, const char* action, const char* inner, std::string* body) {
    Url url;
    if (!ParseUrl(control, &url)) return false;
    char action_urn[256];
    std::snprintf(action_urn, sizeof action_urn, "%s#%s", service, action);
    char payload[1200];
    int pn = std::snprintf(payload, sizeof payload,
                           "<?xml version=\"1.0\"?>"
                           "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                           "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
                           "<s:Body><u:%s xmlns:u=\"%s\">%s</u:%s></s:Body></s:Envelope>",
                           action, service, inner ? inner : "", action);
    if (pn <= 0 || pn >= int(sizeof payload)) return false;
    std::string raw;
    if (!Http(url, payload, action_urn, &raw)) return false;
    *body = BodyOf(raw);
    if (body->find("Fault") != std::string::npos) return false;
    return true;
}

bool ControlUrl(const std::string& xml, const char* service_tail, char* service, int service_n, char* url, int url_n) {
    const char* p = std::strstr(xml.c_str(), service_tail);
    if (!p) return false;
    const char* close = std::strchr(p, '>');
    if (!close) return false;
    const char* urn = close;
    while (urn > xml.c_str() && urn[-1] != '>') urn--;
    const char* urn_end = std::strchr(p, '<');
    if (!urn_end || urn_end <= urn) return false;
    while (urn < urn_end && (*urn == ' ' || *urn == '\n' || *urn == '\r' || *urn == '\t')) urn++;
    int un = int(urn_end - urn);
    if (un <= 0 || un >= service_n) return false;
    std::memcpy(service, urn, size_t(un));
    service[un] = '\0';
    while (un > 0 && (service[un - 1] == ' ' || service[un - 1] == '\n' || service[un - 1] == '\r')) service[--un] = '\0';
    if (std::strstr(service, service_tail) == nullptr) return false;
    const char* next = std::strstr(urn_end, "serviceType");
    const char* slice_end = next ? next : xml.c_str() + xml.size();
    std::string slice(urn_end, slice_end);
    return TagText(slice, "controlURL", url, url_n) && url[0];
}

bool Discover(char locations[][512], int* count, int cap) {
    *count = 0;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;
    int ttl = 2;
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl);
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(1900);
    inet_pton(AF_INET, "239.255.255.250", &dest.sin_addr);
    const char* sts[] = {
        "urn:schemas-upnp-org:device:InternetGatewayDevice:1",
        "urn:schemas-upnp-org:service:WANIPConnection:1",
        "urn:schemas-upnp-org:service:WANPPPConnection:1",
    };
    for (const char* st : sts) {
        char pkt[400];
        int n = std::snprintf(pkt, sizeof pkt,
                              "M-SEARCH * HTTP/1.1\r\n"
                              "HOST: 239.255.255.250:1900\r\n"
                              "MAN: \"ssdp:discover\"\r\n"
                              "MX: 1\r\n"
                              "ST: %s\r\n"
                              "\r\n",
                              st);
        if (n > 0) sendto(fd, pkt, size_t(n), 0, reinterpret_cast<sockaddr*>(&dest), sizeof dest);
    }
    char buf[2048];
    for (;;) {
        pollfd pfd{fd, POLLIN, 0};
        if (poll(&pfd, 1, *count == 0 ? 1500 : 400) <= 0) break;
        sockaddr_in from{};
        socklen_t fl = sizeof from;
        ssize_t n = recvfrom(fd, buf, sizeof buf - 1, 0, reinterpret_cast<sockaddr*>(&from), &fl);
        if (n <= 0) continue;
        buf[n] = '\0';
        char loc[512];
        if (!LocationOf(buf, loc, int(sizeof loc))) continue;
        bool seen = false;
        for (int i = 0; i < *count; i++)
            if (std::strcmp(locations[i], loc) == 0) seen = true;
        if (seen || *count >= cap) continue;
        std::snprintf(locations[*count], 512, "%s", loc);
        (*count)++;
    }
    close(fd);
    return *count > 0;
}

bool AddUpnp(MapState* st, uint16_t local_port) {
    char locations[4][512];
    int count = 0;
    if (!Discover(locations, &count, 4)) {
        std::fprintf(stderr, "port_map: no UPnP reply\n");
        return false;
    }
    for (int i = 0; i < count; i++) {
        Url desc;
        if (!ParseUrl(locations[i], &desc)) continue;
        std::string raw, xml;
        if (!Http(desc, nullptr, nullptr, &raw)) continue;
        xml = BodyOf(raw);
        const char* tails[] = {"WANIPConnection:1", "WANPPPConnection:1"};
        for (const char* tail : tails) {
            char service[128];
            char control_rel[512];
            if (!ControlUrl(xml, tail, service, int(sizeof service), control_rel, int(sizeof control_rel))) continue;
            char control[700];
            if (!JoinUrl(locations[i], control_rel, control, int(sizeof control))) continue;
            if (!LocalIp(desc.host, desc.port, st->client, int(sizeof st->client))) continue;
            std::string body;
            if (!Soap(control, service, "GetExternalIPAddress", "", &body)) continue;
            char ip[64];
            if (!TagText(body, "NewExternalIPAddress", ip, int(sizeof ip))) continue;
            if (!PublicIPv4(ip)) {
                in_addr parsed{};
                if (inet_pton(AF_INET, ip, &parsed) == 1) st->kind = -1;
                continue;
            }
            char inner[400];
            std::snprintf(inner, sizeof inner,
                          "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort>"
                          "<NewProtocol>UDP</NewProtocol><NewInternalPort>%u</NewInternalPort>"
                          "<NewInternalClient>%s</NewInternalClient><NewEnabled>1</NewEnabled>"
                          "<NewPortMappingDescription>RingHost</NewPortMappingDescription>"
                          "<NewLeaseDuration>%d</NewLeaseDuration>",
                          unsigned(local_port), unsigned(local_port), st->client, kLease);
            if (!Soap(control, service, "AddPortMapping", inner, &body)) continue;
            st->kind = 1;
            st->local_port = local_port;
            st->public_port = local_port;
            std::snprintf(st->public_ip, sizeof st->public_ip, "%s", ip);
            std::snprintf(st->service, sizeof st->service, "%s", service);
            std::snprintf(st->control, sizeof st->control, "%s", control);
            return true;
        }
    }
    return false;
}

bool Gateway(char* out, int n) {
#if defined(__APPLE__)
    int mib[6] = {CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_FLAGS, RTF_GATEWAY};
    size_t len = 0;
    if (sysctl(mib, 6, nullptr, &len, nullptr, 0) == 0 && len > 0) {
        std::vector<char> buf(len);
        if (sysctl(mib, 6, buf.data(), &len, nullptr, 0) == 0) {
            char* p = buf.data();
            char* end = p + len;
            while (p + sizeof(rt_msghdr) <= end) {
                auto* hdr = reinterpret_cast<rt_msghdr*>(p);
                if (hdr->rtm_msglen < sizeof(rt_msghdr) || p + hdr->rtm_msglen > end) break;
                if ((hdr->rtm_flags & (RTF_UP | RTF_GATEWAY)) == (RTF_UP | RTF_GATEWAY) &&
                    (hdr->rtm_addrs & (RTA_DST | RTA_GATEWAY)) == (RTA_DST | RTA_GATEWAY)) {
                    char* ap = p + sizeof(rt_msghdr);
                    auto* dst = reinterpret_cast<sockaddr*>(ap);
                    int dlen = dst->sa_len > 0 ? dst->sa_len : int(sizeof(sockaddr));
                    dlen = (dlen + 3) & ~3;
                    if (dst->sa_family == AF_INET) {
                        auto* din = reinterpret_cast<sockaddr_in*>(dst);
                        if (din->sin_addr.s_addr == 0 && ap + dlen + int(sizeof(sockaddr_in)) <= p + hdr->rtm_msglen) {
                            auto* gw = reinterpret_cast<sockaddr_in*>(ap + dlen);
                            if (gw->sin_family == AF_INET && gw->sin_addr.s_addr != 0 &&
                                inet_ntop(AF_INET, &gw->sin_addr, out, socklen_t(n))) {
                                return true;
                            }
                        }
                    }
                }
                p += hdr->rtm_msglen;
            }
        }
    }
    FILE* f = popen("/sbin/route -n get default", "r");
    if (!f) return false;
    char line[256];
    bool ok = false;
    while (std::fgets(line, int(sizeof line), f)) {
        unsigned a, b, c, d;
        if (std::sscanf(line, " gateway: %u.%u.%u.%u", &a, &b, &c, &d) == 4 && a <= 255 && b <= 255 && c <= 255 &&
            d <= 255) {
            std::snprintf(out, size_t(n), "%u.%u.%u.%u", a, b, c, d);
            ok = true;
            break;
        }
    }
    pclose(f);
    return ok;
#else
    FILE* f = std::fopen("/proc/net/route", "r");
    if (!f) return false;
    char line[256];
    bool ok = false;
    if (std::fgets(line, int(sizeof line), f)) {
        while (std::fgets(line, int(sizeof line), f)) {
            unsigned dest = 0, gw = 0, flags = 0;
            if (std::sscanf(line, "%*s %x %x %x", &dest, &gw, &flags) != 3) continue;
            if (dest != 0 || gw == 0 || (flags & 0x2) == 0) continue;
            in_addr addr{};
            addr.s_addr = gw;
            ok = inet_ntop(AF_INET, &addr, out, socklen_t(n)) != nullptr;
            break;
        }
    }
    std::fclose(f);
    return ok;
#endif
}

bool PmpExchange(const char* gateway, const uint8_t* req, int req_n, uint8_t* resp, int resp_n) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(5351);
    if (inet_pton(AF_INET, gateway, &to.sin_addr) != 1) {
        close(fd);
        return false;
    }
    int wait_ms = 250;
    bool ok = false;
    for (int attempt = 0; attempt < 4 && !ok; attempt++) {
        if (sendto(fd, req, size_t(req_n), 0, reinterpret_cast<sockaddr*>(&to), sizeof to) != req_n) break;
        pollfd pfd{fd, POLLIN, 0};
        if (poll(&pfd, 1, wait_ms) <= 0) {
            wait_ms *= 2;
            continue;
        }
        sockaddr_in from{};
        socklen_t fl = sizeof from;
        ssize_t n = recvfrom(fd, resp, size_t(resp_n), 0, reinterpret_cast<sockaddr*>(&from), &fl);
        if (n == resp_n && from.sin_addr.s_addr == to.sin_addr.s_addr) ok = true;
        else wait_ms *= 2;
    }
    close(fd);
    return ok;
}

bool RandomBytes(uint8_t* out, int n) {
    if (!out || n <= 0) return false;
#if defined(__APPLE__)
    arc4random_buf(out, size_t(n));
    return true;
#else
    FILE* f = std::fopen("/dev/urandom", "rb");
    if (!f) return false;
    size_t got = std::fread(out, 1, size_t(n), f);
    std::fclose(f);
    return int(got) == n;
#endif
}

// PCP shares UDP 5351 with NAT-PMP. The version byte is 2, so a NAT-PMP
// server (version 0) does not treat this as its own request.
bool PcpExchange(const char* gateway, const char* client, const uint8_t* req, int req_n, uint8_t* resp, int resp_cap,
                 int* got) {
    if (got) *got = 0;
    if (!gateway || !client || !req || req_n <= 0 || !resp || resp_cap < 24) return false;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;
    sockaddr_in local{};
    local.sin_family = AF_INET;
#if defined(__APPLE__)
    local.sin_len = uint8_t(sizeof local);
#endif
    if (inet_pton(AF_INET, client, &local.sin_addr) != 1 ||
        bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof local) != 0) {
        close(fd);
        return false;
    }
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(5351);
#if defined(__APPLE__)
    to.sin_len = uint8_t(sizeof to);
#endif
    if (inet_pton(AF_INET, gateway, &to.sin_addr) != 1) {
        close(fd);
        return false;
    }
    int wait_ms = 250;
    bool ok = false;
    for (int attempt = 0; attempt < 4 && !ok; attempt++) {
        if (sendto(fd, req, size_t(req_n), 0, reinterpret_cast<sockaddr*>(&to), sizeof to) != req_n) break;
        pollfd pfd{fd, POLLIN, 0};
        if (poll(&pfd, 1, wait_ms) <= 0) {
            wait_ms *= 2;
            continue;
        }
        sockaddr_in from{};
        socklen_t fl = sizeof from;
        ssize_t n = recvfrom(fd, resp, size_t(resp_cap), 0, reinterpret_cast<sockaddr*>(&from), &fl);
        if (n >= 24 && n <= resp_cap && from.sin_addr.s_addr == to.sin_addr.s_addr) {
            if (got) *got = int(n);
            ok = true;
        } else {
            wait_ms *= 2;
        }
    }
    close(fd);
    return ok;
}

bool SendPcp(const char* gateway, const uint8_t nonce[12], uint16_t internal_port, uint16_t external_port,
             uint32_t lifetime, uint8_t* resp, int cap, int* got) {
    char client[64];
    if (!LocalIp(gateway, 5351, client, int(sizeof client))) return false;
    uint8_t req[60];
    if (!BuildPcpMap(req, int(sizeof req), nonce, client, internal_port, external_port, lifetime)) return false;
    return PcpExchange(gateway, client, req, int(sizeof req), resp, cap, got);
}

bool AddPmp(MapState* st, uint16_t local_port, bool* saw_private, bool* answered) {
    char gw[64];
    if (!Gateway(gw, int(sizeof gw))) {
        std::fprintf(stderr, "port_map: no default gateway\n");
        return false;
    }
    uint8_t ext_req[2] = {0, 0};
    uint8_t ext_resp[12];
    if (!PmpExchange(gw, ext_req, 2, ext_resp, 12)) {
        std::fprintf(stderr, "port_map: nat-pmp %s did not answer\n", gw);
        return false;
    }
    if (answered) *answered = true;
    if (ext_resp[0] != 0 || ext_resp[1] != 128) return false;
    int result = (int(ext_resp[2]) << 8) | int(ext_resp[3]);
    if (result != 0) return false;
    char ip[16];
    if (!inet_ntop(AF_INET, ext_resp + 8, ip, sizeof ip)) return false;
    if (!PublicIPv4(ip)) {
        *saw_private = true;
        std::snprintf(st->public_ip, sizeof st->public_ip, "%s", ip);
        return false;
    }
    uint8_t map[12] = {};
    map[0] = 0;
    map[1] = 1;
    map[4] = uint8_t(local_port >> 8);
    map[5] = uint8_t(local_port);
    map[6] = map[4];
    map[7] = map[5];
    uint32_t life = htonl(uint32_t(kLease));
    std::memcpy(map + 8, &life, 4);
    uint8_t resp[16];
    if (!PmpExchange(gw, map, 12, resp, 16)) return false;
    if (resp[0] != 0 || resp[1] != 129) return false;
    result = (int(resp[2]) << 8) | int(resp[3]);
    if (result != 0) return false;
    unsigned pub = (unsigned(resp[10]) << 8) | unsigned(resp[11]);
    if (pub == 0 || pub > 65535) return false;
    st->kind = 2;
    st->local_port = local_port;
    st->public_port = uint16_t(pub);
    std::snprintf(st->public_ip, sizeof st->public_ip, "%s", ip);
    std::snprintf(st->gateway, sizeof st->gateway, "%s", gw);
    return true;
}

bool AddPcp(MapState* st, uint16_t local_port, bool* saw_private) {
    char gw[64];
    if (!Gateway(gw, int(sizeof gw))) {
        std::fprintf(stderr, "port_map: no default gateway\n");
        return false;
    }
    uint8_t nonce[12];
    if (!RandomBytes(nonce, 12)) return false;
    uint8_t resp[128];
    int got = 0;
    if (!SendPcp(gw, nonce, local_port, local_port, uint32_t(kLease), resp, int(sizeof resp), &got)) {
        std::fprintf(stderr, "port_map: pcp %s did not answer\n", gw);
        return false;
    }
    char ip[16];
    uint16_t pub = 0;
    uint32_t life = 0;
    if (!ParsePcpMap(resp, got, nonce, local_port, ip, int(sizeof ip), &pub, &life)) return false;
    if (!PublicIPv4(ip)) {
        *saw_private = true;
        std::snprintf(st->public_ip, sizeof st->public_ip, "%s", ip);
        if (life > 0) {
            uint8_t drop[128];
            int dropped = 0;
            SendPcp(gw, nonce, local_port, 0, 0, drop, int(sizeof drop), &dropped);
        }
        return false;
    }
    if (life == 0) return false;
    st->kind = 3;
    st->local_port = local_port;
    st->public_port = pub;
    std::snprintf(st->public_ip, sizeof st->public_ip, "%s", ip);
    std::snprintf(st->gateway, sizeof st->gateway, "%s", gw);
    std::memcpy(st->nonce, nonce, 12);
    return true;
}

bool RenewUpnp(const MapState& st) {
    char inner[400];
    std::snprintf(inner, sizeof inner,
                  "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort>"
                  "<NewProtocol>UDP</NewProtocol><NewInternalPort>%u</NewInternalPort>"
                  "<NewInternalClient>%s</NewInternalClient><NewEnabled>1</NewEnabled>"
                  "<NewPortMappingDescription>RingHost</NewPortMappingDescription>"
                  "<NewLeaseDuration>%d</NewLeaseDuration>",
                  unsigned(st.public_port), unsigned(st.local_port), st.client, kLease);
    std::string body;
    return Soap(st.control, st.service, "AddPortMapping", inner, &body);
}

bool RenewPmp(const MapState& st) {
    uint8_t map[12] = {};
    map[1] = 1;
    map[4] = uint8_t(st.local_port >> 8);
    map[5] = uint8_t(st.local_port);
    map[6] = uint8_t(st.public_port >> 8);
    map[7] = uint8_t(st.public_port);
    uint32_t life = htonl(uint32_t(kLease));
    std::memcpy(map + 8, &life, 4);
    uint8_t resp[16];
    if (!PmpExchange(st.gateway, map, 12, resp, 16)) return false;
    return resp[0] == 0 && resp[1] == 129 && resp[2] == 0 && resp[3] == 0;
}

bool RenewPcp(const MapState& st) {
    uint8_t resp[128];
    int got = 0;
    if (!SendPcp(st.gateway, st.nonce, st.local_port, st.public_port, uint32_t(kLease), resp, int(sizeof resp), &got))
        return false;
    char ip[16];
    uint16_t pub = 0;
    uint32_t life = 0;
    if (!ParsePcpMap(resp, got, st.nonce, st.local_port, ip, int(sizeof ip), &pub, &life)) return false;
    return life > 0 && pub == st.public_port && PublicIPv4(ip);
}

// Lifetime 0 deletes the PCP MAP (RFC 6887). The nonce and internal port name it.
// A lost delete still ends when the one hour lease runs out.
void UnmapPcp(const MapState& st) {
    uint8_t resp[128];
    int got = 0;
    SendPcp(st.gateway, st.nonce, st.local_port, 0, 0, resp, int(sizeof resp), &got);
}

void Unmap(const MapState& st) {
    if (st.kind == 1 && st.control[0] && st.service[0]) {
        char inner[200];
        std::snprintf(inner, sizeof inner,
                      "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort><NewProtocol>UDP</NewProtocol>",
                      unsigned(st.public_port));
        std::string body;
        Soap(st.control, st.service, "DeletePortMapping", inner, &body);
    } else if (st.kind == 2 && st.gateway[0]) {
        uint8_t map[12] = {};
        map[1] = 1;
        map[4] = uint8_t(st.local_port >> 8);
        map[5] = uint8_t(st.local_port);
        uint8_t resp[16];
        PmpExchange(st.gateway, map, 12, resp, 16);
    } else if (st.kind == 3 && st.gateway[0]) {
        UnmapPcp(st);
    }
}

void Renew(const MapState& st) {
    if (st.kind == 1) RenewUpnp(st);
    else if (st.kind == 2) RenewPmp(st);
    else if (st.kind == 3) RenewPcp(st);
}

void NonceHex(const uint8_t nonce[12], char* out, int n) {
    static const char* hexd = "0123456789abcdef";
    if (!out || n < 25) {
        if (out && n > 0) out[0] = '\0';
        return;
    }
    for (int i = 0; i < 12; i++) {
        out[i * 2] = hexd[nonce[i] >> 4];
        out[i * 2 + 1] = hexd[nonce[i] & 0xf];
    }
    out[24] = '\0';
}

int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool NonceFromHex(const char* text, uint8_t nonce[12]) {
    if (!text || std::strlen(text) != 24) return false;
    for (int i = 0; i < 12; i++) {
        int hi = HexVal(text[i * 2]);
        int lo = HexVal(text[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        nonce[i] = uint8_t((hi << 4) | lo);
    }
    return true;
}

bool MapLine(const MapState& st, char* line, int n) {
    if (!line || n < 8) return false;
    int wrote = -1;
    if (st.kind == 1) {
        wrote = std::snprintf(line, size_t(n), "upnp %u %u %s %s %s", unsigned(st.public_port), unsigned(st.local_port),
                              st.client, st.service, st.control);
    } else if (st.kind == 2) {
        wrote = std::snprintf(line, size_t(n), "pmp %s %u %u", st.gateway, unsigned(st.public_port),
                              unsigned(st.local_port));
    } else if (st.kind == 3) {
        char hex[25];
        NonceHex(st.nonce, hex, int(sizeof hex));
        wrote = std::snprintf(line, size_t(n), "pcp %s %u %u %s", st.gateway, unsigned(st.public_port),
                              unsigned(st.local_port), hex);
    }
    return wrote > 0 && wrote < n;
}

bool ParseMap(const char* line, MapState* st) {
    if (!line || !st) return false;
    unsigned pub = 0, loc = 0;
    char client[64] = {};
    char service[128] = {};
    char control[512] = {};
    char gw[64] = {};
    if (std::sscanf(line, "upnp %u %u %63s %127s %511s", &pub, &loc, client, service, control) == 5 && pub > 0 &&
        pub <= 65535 && loc > 0 && loc <= 65535 && std::strncmp(service, "urn:", 4) == 0 &&
        std::strncmp(control, "http://", 7) == 0) {
        st->kind = 1;
        st->public_port = uint16_t(pub);
        st->local_port = uint16_t(loc);
        std::snprintf(st->client, sizeof st->client, "%s", client);
        std::snprintf(st->service, sizeof st->service, "%s", service);
        std::snprintf(st->control, sizeof st->control, "%s", control);
        return true;
    }
    if (std::sscanf(line, "pmp %63s %u %u", gw, &pub, &loc) == 3 && pub > 0 && pub <= 65535 && loc > 0 && loc <= 65535) {
        in_addr addr{};
        if (inet_pton(AF_INET, gw, &addr) != 1) return false;
        st->kind = 2;
        st->public_port = uint16_t(pub);
        st->local_port = uint16_t(loc);
        std::snprintf(st->gateway, sizeof st->gateway, "%s", gw);
        return true;
    }
    char nonce_hex[32] = {};
    if (std::sscanf(line, "pcp %63s %u %u %24s", gw, &pub, &loc, nonce_hex) == 4 && pub > 0 && pub <= 65535 && loc > 0 &&
        loc <= 65535) {
        in_addr addr{};
        if (inet_pton(AF_INET, gw, &addr) != 1) return false;
        if (!NonceFromHex(nonce_hex, st->nonce)) return false;
        st->kind = 3;
        st->public_port = uint16_t(pub);
        st->local_port = uint16_t(loc);
        std::snprintf(st->gateway, sizeof st->gateway, "%s", gw);
        return true;
    }
    return false;
}

void ClearEnv() {
    unsetenv("FUZION_RELAY_PID");
    unsetenv("FUZION_RELAY_BORN");
    unsetenv("FUZION_RELAY_GUARD");
    unsetenv("FUZION_RELAY_GUARD_BORN");
    unsetenv("FUZION_PORT_MAP");
}

void PublishEnv(const MapState& st) {
    char pid[16], guard[16], line[900];
    std::snprintf(pid, sizeof pid, "%d", st.relay_pid);
    std::snprintf(guard, sizeof guard, "%d", st.guard_pid);
    if (!MapLine(st, line, int(sizeof line))) return;
    setenv("FUZION_RELAY_PID", pid, 1);
    setenv("FUZION_RELAY_BORN", st.relay_start, 1);
    setenv("FUZION_RELAY_GUARD", guard, 1);
    setenv("FUZION_RELAY_GUARD_BORN", st.guard_start, 1);
    setenv("FUZION_PORT_MAP", line, 1);
}

void LoadEnv(MapState* st) {
    const char* map = std::getenv("FUZION_PORT_MAP");
    const char* pid = std::getenv("FUZION_RELAY_PID");
    const char* born = std::getenv("FUZION_RELAY_BORN");
    const char* guard = std::getenv("FUZION_RELAY_GUARD");
    const char* gborn = std::getenv("FUZION_RELAY_GUARD_BORN");
    if (!map || !pid || !ParseMap(map, st)) return;
    st->relay_pid = std::atoi(pid);
    if (born) std::snprintf(st->relay_start, sizeof st->relay_start, "%s", born);
    if (guard) st->guard_pid = std::atoi(guard);
    if (gborn) std::snprintf(st->guard_start, sizeof st->guard_start, "%s", gborn);
}

void ArmExit() {
    static std::once_flag once;
    std::call_once(once, [] { std::atexit(StopHostedRelay); });
}

void EnsureRefresh() {
    static std::once_flag once;
    std::call_once(once, [] {
        std::thread([] {
            int wait_s = 1;
            while (true) {
                int seen = g_gen.load(std::memory_order_acquire);
                for (int i = 0; i < wait_s; i++) {
                    if (g_gen.load(std::memory_order_acquire) != seen) break;
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
                wait_s = 120;
                std::lock_guard<std::mutex> lock(g_mu);
                if (g_gen.load(std::memory_order_relaxed) == seen && g_state.kind > 0) Renew(g_state);
            }
        }).detach();
    });
}

bool SpawnRelay(const char* dir, int* pid, char* born, int born_n, int* port) {
    char path[1024];
    if (std::snprintf(path, sizeof path, "%s/relay_server", dir) >= int(sizeof path)) return false;
    if (access(path, X_OK) != 0) return false;
    int pipes[2];
    if (pipe(pipes) != 0) return false;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, pipes[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&fa, pipes[0]);
    posix_spawn_file_actions_addclose(&fa, pipes[1]);
    char arg_port[] = "0";
    char arg_cap[] = "1";
    char* argv[] = {path, arg_port, arg_cap, nullptr};
    pid_t child = 0;
    int rc = posix_spawn(&child, path, &fa, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(pipes[1]);
    if (rc != 0) {
        close(pipes[0]);
        return false;
    }
    std::string line;
    while (line.find('\n') == std::string::npos && line.size() < 300) {
        pollfd pfd{pipes[0], POLLIN, 0};
        if (poll(&pfd, 1, 3000) <= 0) break;
        char buf[128];
        ssize_t n = read(pipes[0], buf, sizeof buf);
        if (n <= 0) break;
        line.append(buf, buf + n);
    }
    close(pipes[0]);
    int got = 0;
    if (std::sscanf(line.c_str(), "Relay listening on port %d", &got) != 1 || got <= 0 || got > 65535) {
        kill(child, SIGKILL);
        Reap(int(child));
        return false;
    }
    *pid = int(child);
    *port = got;
    ProcessStart(*pid, born, born_n);
    return true;
}

bool SpawnGuard(const char* dir, int parent, const char* parent_start, int relay, const char* relay_start, int* guard,
                char* guard_start, int guard_start_n, int* write_fd) {
    char path[1024];
    if (std::snprintf(path, sizeof path, "%s/relay_guard", dir) >= int(sizeof path)) return false;
    if (access(path, X_OK) != 0) return false;
    int pipes[2];
    if (pipe(pipes) != 0) return false;
    fcntl(pipes[0], F_SETFD, FD_CLOEXEC);
    fcntl(pipes[1], F_SETFD, FD_CLOEXEC);
    int devnull = open("/dev/null", O_RDWR);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, pipes[0], STDIN_FILENO);
    if (devnull >= 0) posix_spawn_file_actions_adddup2(&fa, devnull, STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&fa, pipes[0]);
    posix_spawn_file_actions_addclose(&fa, pipes[1]);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attr, 0);
    char ppid_s[16], rpid_s[16];
    std::snprintf(ppid_s, sizeof ppid_s, "%d", parent);
    std::snprintf(rpid_s, sizeof rpid_s, "%d", relay);
    char parent_born[80], relay_born[80];
    std::snprintf(parent_born, sizeof parent_born, "%s", parent_start ? parent_start : "");
    std::snprintf(relay_born, sizeof relay_born, "%s", relay_start ? relay_start : "");
    char* argv[] = {path, ppid_s, parent_born, rpid_s, relay_born, nullptr};
    pid_t child = 0;
    int rc = posix_spawn(&child, path, &fa, &attr, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);
    if (devnull >= 0) close(devnull);
    close(pipes[0]);
    if (rc != 0) {
        close(pipes[1]);
        return false;
    }
    *guard = int(child);
    *write_fd = pipes[1];
    ProcessStart(*guard, guard_start, guard_start_n);
    return true;
}

bool WriteLine(int fd, const char* line) {
    int n = int(std::strlen(line));
    int off = 0;
    while (off < n) {
        ssize_t w = write(fd, line + off, size_t(n - off));
        if (w < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        off += int(w);
    }
    return true;
}

}  // namespace

void StopHostedRelay() {
    MapState local;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        local = g_state;
        g_state = {};
        g_gen.fetch_add(1, std::memory_order_acq_rel);
    }
    KillPid(local.guard_pid, local.guard_start, false);
    if (local.kind > 0) Unmap(local);
    KillPid(local.relay_pid, local.relay_start, true);
    ClearEnv();
}

void InstallHostedRelay() {
    MapState loaded;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        LoadEnv(&loaded);
        if (loaded.kind > 0) g_state = loaded;
    }
    if (loaded.kind > 0 && loaded.relay_pid > 1 && !SameProcess(loaded.relay_pid, loaded.relay_start)) {
        StopHostedRelay();
    }
    bool live = false;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        live = g_state.kind > 0;
    }
    if (!live) return;
    ArmExit();
    EnsureRefresh();
}

bool StartHostedRelay(const char* exe_dir, HostedRelay* out, char* error, int error_n) {
    if (out) *out = {};
    if (!exe_dir || !exe_dir[0] || !out) {
        SetErr(error, error_n, kNoRelay);
        return false;
    }
    StopHostedRelay();
    int relay = -1;
    int port = 0;
    char relay_start[80] = {};
    if (!SpawnRelay(exe_dir, &relay, relay_start, int(sizeof relay_start), &port)) {
        SetErr(error, error_n, kNoRelay);
        return false;
    }
    char self_start[80] = {};
    ProcessStart(int(getpid()), self_start, int(sizeof self_start));
    int guard = -1;
    int wake = -1;
    char guard_start[80] = {};
    bool have_guard = SpawnGuard(exe_dir, int(getpid()), self_start, relay, relay_start, &guard, guard_start,
                                 int(sizeof guard_start), &wake);
    MapState mapped;
    bool saw_private = false;
    bool up = false;
    try {
        up = AddUpnp(&mapped, uint16_t(port));
        if (!up && mapped.kind == -1) saw_private = true;
        // UPnP, then NAT-PMP. PCP only when the gateway sent no NAT-PMP reply.
        if (!up) {
            MapState pmp;
            bool pmp_answered = false;
            if (AddPmp(&pmp, uint16_t(port), &saw_private, &pmp_answered)) {
                mapped = pmp;
                up = true;
            } else if (pmp.public_ip[0] && !PublicIPv4(pmp.public_ip)) {
                saw_private = true;
            }
            if (!up && !pmp_answered) {
                MapState pcp;
                if (AddPcp(&pcp, uint16_t(port), &saw_private)) {
                    mapped = pcp;
                    up = true;
                } else if (pcp.public_ip[0] && !PublicIPv4(pcp.public_ip)) {
                    saw_private = true;
                }
            }
        }
    } catch (...) {
        up = false;
    }
    if (!up || !PublicIPv4(mapped.public_ip)) {
        if (wake >= 0) close(wake);
        KillPid(guard, guard_start, false);
        KillPid(relay, relay_start, true);
        std::fprintf(stderr, "port_map: router did not give a public port\n");
        SetErr(error, error_n, saw_private ? kNoInet : kNoPort);
        return false;
    }
    mapped.relay_pid = relay;
    mapped.guard_pid = guard;
    std::snprintf(mapped.relay_start, sizeof mapped.relay_start, "%s", relay_start);
    std::snprintf(mapped.guard_start, sizeof mapped.guard_start, "%s", guard_start);
    char line[900];
    if (have_guard && wake >= 0) {
        if (MapLine(mapped, line, int(sizeof line) - 2)) {
            int n = int(std::strlen(line));
            line[n] = '\n';
            line[n + 1] = '\0';
            if (!WriteLine(wake, line)) std::fprintf(stderr, "port_map: guard did not take the mapping\n");
        } else {
            std::fprintf(stderr, "port_map: guard did not take the mapping\n");
        }
        close(wake);
    }
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_state = mapped;
        g_gen.fetch_add(1, std::memory_order_acq_rel);
    }
    PublishEnv(mapped);
    ArmExit();
    EnsureRefresh();
    out->local_port = mapped.local_port;
    out->public_port = mapped.public_port;
    std::snprintf(out->public_ip, sizeof out->public_ip, "%s", mapped.public_ip);
    (void)have_guard;
    return true;
}

int GuardMain(int argc, char** argv) {
    signal(SIGHUP, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    if (argc < 5) return 1;
    int parent = std::atoi(argv[1]);
    const char* parent_start = argv[2];
    int relay = std::atoi(argv[3]);
    const char* relay_start = argv[4];
    char line[1200];
    line[0] = '\0';
    int got = 0;
    bool have = false;
    while (SameProcess(parent, parent_start)) {
        if (!have) {
            pollfd pfd{STDIN_FILENO, POLLIN, 0};
            int pr = poll(&pfd, 1, 500);
            if (pr > 0) {
                char buf[256];
                ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
                if (n > 0) {
                    int room = int(sizeof line) - 1 - got;
                    if (room < 0) room = 0;
                    if (n > room) n = room;
                    if (n > 0) std::memcpy(line + got, buf, size_t(n));
                    got += int(n);
                    line[got] = '\0';
                    if (std::strchr(line, '\n')) have = true;
                } else if (n == 0) {
                    have = true;
                }
            }
        } else {
            poll(nullptr, 0, 500);
        }
    }
    MapState st;
    if (ParseMap(line, &st)) Unmap(st);
    KillPid(relay, relay_start, true);
    return 0;
}

}  // namespace net

#endif
