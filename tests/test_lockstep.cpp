// Two peers, a dropped datagram, a bad hash, a silent peer, and a UDP pair.
#include "net/lockstep.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static int g_fails = 0;

static void Expect(bool ok, const char* what) {
    if (ok) return;
    std::fprintf(stderr, "FAIL %s\n", what);
    g_fails++;
}

static bool Same(const net::Report& a, const net::Report& b) {
    if (a.connected != b.connected || a.buttons != b.buttons) return false;
    if (a.lx != b.lx || a.ly != b.ly || a.rx != b.rx || a.ry != b.ry) return false;
    return std::memcmp(a.analog, b.analog, 8) == 0;
}

// Send writes outbox. Recv drains inbox. drop_state drops that many type-2 packets.
struct QueuePipe : net::Datagram {
    std::mutex* send_mu = nullptr;
    std::vector<std::vector<uint8_t>>* outbox = nullptr;
    std::mutex* recv_mu = nullptr;
    std::vector<std::vector<uint8_t>>* inbox = nullptr;
    int* drop_state = nullptr;

    bool Send(const uint8_t* data, int n) override {
        if (!data || n <= 0) return false;
        std::lock_guard<std::mutex> lock(*send_mu);
        if (drop_state && *drop_state > 0 && n >= 5 && data[4] == 2) {
            (*drop_state)--;
            return true;
        }
        outbox->emplace_back(data, data + n);
        return true;
    }
    bool Recv(std::vector<std::vector<uint8_t>>& out) override {
        std::lock_guard<std::mutex> lock(*recv_mu);
        if (inbox->empty()) return true;
        out.insert(out.end(), inbox->begin(), inbox->end());
        inbox->clear();
        return true;
    }
};

struct Link {
    std::mutex mu_a;
    std::mutex mu_b;
    std::vector<std::vector<uint8_t>> inbox_a;
    std::vector<std::vector<uint8_t>> inbox_b;
    int drop_to_a = 0;
    int drop_to_b = 0;
};

static net::Config Base(int seat) {
    net::Config c;
    c.seat = seat;
    c.delay = 2;
    c.timeout_ms = 2000;
    c.hello_ms = 2000;
    c.seed = 1;
    c.width = 1280;
    c.height = 720;
    c.ultrawide = 1;
    return c;
}

static net::Report Local(int seat, uint64_t frame) {
    net::Report r;
    r.connected = 1;
    if (seat == 1) r.buttons = uint16_t(frame + 1);
    else r.analog[0] = uint8_t(frame);
    return r;
}

static void Wire(Link& link, net::Session& a, net::Session& b) {
    auto pa = std::make_unique<QueuePipe>();
    auto pb = std::make_unique<QueuePipe>();
    pa->send_mu = &link.mu_b;
    pa->outbox = &link.inbox_b;
    pa->recv_mu = &link.mu_a;
    pa->inbox = &link.inbox_a;
    pa->drop_state = &link.drop_to_b;
    pb->send_mu = &link.mu_a;
    pb->outbox = &link.inbox_a;
    pb->recv_mu = &link.mu_b;
    pb->inbox = &link.inbox_b;
    pb->drop_state = &link.drop_to_a;
    a.SetPipe(std::move(pa));
    b.SetPipe(std::move(pb));
}

static void TestMatch(int dropped) {
    Link link;
    link.drop_to_b = dropped;
    net::Session a(Base(1));
    net::Session b(Base(2));
    Wire(link, a, b);
    std::atomic<int> bad{0};
    std::vector<net::Report> pads_a(40 * 4);
    std::vector<net::Report> pads_b(40 * 4);
    std::thread ta([&] {
        for (uint64_t f = 0; f < 40; f++) {
            net::Result r = a.Agree(f, Local(1, f));
            if (r.fail != net::Fail::None) bad++;
            else
                for (int p = 0; p < 4; p++) pads_a[size_t(f) * 4 + p] = r.pad[p];
            if (a.NoteHash(f, 0x1000 + f).fail != net::Fail::None) bad++;
        }
        if (a.Drain(39).fail != net::Fail::None) bad++;
    });
    std::thread tb([&] {
        for (uint64_t f = 0; f < 40; f++) {
            net::Result r = b.Agree(f, Local(2, f));
            if (r.fail != net::Fail::None) bad++;
            else
                for (int p = 0; p < 4; p++) pads_b[size_t(f) * 4 + p] = r.pad[p];
            if (b.NoteHash(f, 0x1000 + f).fail != net::Fail::None) bad++;
        }
        if (b.Drain(39).fail != net::Fail::None) bad++;
    });
    ta.join();
    tb.join();
    Expect(bad.load() == 0, dropped ? "pair agree after a dropped state" : "pair agree");
    int pad_bad = 0;
    net::Report empty;
    for (uint64_t f = 0; f < 40; f++) {
        for (int p = 0; p < 4; p++)
            if (!Same(pads_a[size_t(f) * 4 + p], pads_b[size_t(f) * 4 + p])) pad_bad++;
        if (f < 2) {
            for (int p = 0; p < 4; p++)
                if (!Same(pads_a[size_t(f) * 4 + p], empty)) pad_bad++;
        } else {
            if (!Same(pads_a[size_t(f) * 4 + 0], Local(1, f - 2))) pad_bad++;
            if (!Same(pads_a[size_t(f) * 4 + 1], Local(2, f - 2))) pad_bad++;
            if (!Same(pads_a[size_t(f) * 4 + 2], empty) || !Same(pads_a[size_t(f) * 4 + 3], empty)) pad_bad++;
        }
    }
    Expect(pad_bad == 0, dropped ? "delayed pads after a drop" : "delayed pads");
}

static void TestDesync() {
    Link link;
    net::Session a(Base(1));
    net::Session b(Base(2));
    Wire(link, a, b);
    std::atomic<int> saw{0};
    auto side = [&](net::Session& s, int seat, uint64_t bad_hash) {
        for (uint64_t f = 0; f < 8; f++) {
            if (s.Agree(f, Local(seat, f)).fail != net::Fail::None) return;
            if (s.NoteHash(f, 10).fail != net::Fail::None) return;
        }
        net::Result r = s.NoteHash(7, bad_hash);
        if (r.fail == net::Fail::None) r = s.Drain(7);
        if (r.fail == net::Fail::Desync && r.frame == 7) saw++;
    };
    std::thread ta([&] { side(a, 1, 1); });
    std::thread tb([&] { side(b, 2, 2); });
    ta.join();
    tb.join();
    Expect(saw.load() >= 1, "hash mismatch is a desync");
}

static void TestHelloMismatch() {
    Link link;
    net::Config ca = Base(1);
    net::Config cb = Base(2);
    cb.seed = 2;
    ca.hello_ms = 1000;
    cb.hello_ms = 1000;
    net::Session a(ca);
    net::Session b(cb);
    Wire(link, a, b);
    std::atomic<int> hello{0};
    std::thread ta([&] {
        if (a.Hello().fail == net::Fail::Hello) hello++;
    });
    std::thread tb([&] {
        if (b.Hello().fail == net::Fail::Hello) hello++;
    });
    ta.join();
    tb.join();
    Expect(hello.load() == 2, "different seeds reject the hello");
}

static void TestUdpTimeout() {
    net::Config c = Base(1);
    c.bind = "127.0.0.1:0";
    c.peer = "127.0.0.1:9";
    c.hello_ms = 80;
    c.timeout_ms = 80;
    net::Session s(c);
    Expect(s.Agree(0, Local(1, 0)).fail == net::Fail::Timeout, "silent peer times out");
}

static void TestUdpPair() {
    net::Config ca = Base(1);
    net::Config cb = Base(2);
    ca.bind = "127.0.0.1:27115";
    ca.peer = "127.0.0.1:27116";
    cb.bind = "127.0.0.1:27116";
    cb.peer = "127.0.0.1:27115";
    net::Session a(ca);
    net::Session b(cb);
    std::atomic<int> bad{0};
    std::thread ta([&] {
        for (uint64_t f = 0; f < 10; f++) {
            net::Result r = a.Agree(f, Local(1, f));
            if (r.fail != net::Fail::None) bad++;
            else if (f >= 2 && !Same(r.pad[1], Local(2, f - 2))) bad++;
        }
    });
    std::thread tb([&] {
        for (uint64_t f = 0; f < 10; f++) {
            net::Result r = b.Agree(f, Local(2, f));
            if (r.fail != net::Fail::None) bad++;
            else if (f >= 2 && !Same(r.pad[0], Local(1, f - 2))) bad++;
        }
    });
    ta.join();
    tb.join();
    Expect(bad.load() == 0, "udp loopback pair");
}

int main() {
    TestMatch(0);
    TestMatch(1);
    TestDesync();
    TestHelloMismatch();
    TestUdpTimeout();
    TestUdpPair();
    if (g_fails) {
        std::fprintf(stderr, "test_lockstep: %d failed\n", g_fails);
        return 1;
    }
    std::printf("test_lockstep: PASS\n");
    return 0;
}
