// The snapshot heap: freelist reuse, and malloc while the arena is off.
#include "det/arena.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static int g_fails = 0;

static void Expect(bool ok, const char* what) {
    if (ok) return;
    std::fprintf(stderr, "FAIL %s\n", what);
    g_fails++;
}

int main() {
    if (!det::ArenaMap()) {
        std::printf("test_arena: arena map failed, skipped\n");
        return 0;
    }
    det::ArenaSetActive(false);
    void* sys = ::operator new(32);
    auto base = reinterpret_cast<uintptr_t>(det::ArenaBase());
    auto cap = det::ArenaCapacity();
    auto in_arena = [&](const void* p) {
        auto a = reinterpret_cast<uintptr_t>(p);
        return a >= base && a < base + cap;
    };
    Expect(!in_arena(sys), "off uses the system heap");
    ::operator delete(sys);

    det::ArenaSetActive(true);
    void* a = ::operator new(24);
    void* b = ::operator new(1000);
    Expect(in_arena(a), "small alloc is in the arena");
    Expect(in_arena(b), "medium alloc is in the arena");
    Expect(a != b, "two allocs differ");
    std::memset(a, 0xAB, 24);
    std::memset(b, 0xCD, 1000);
    Expect(std::memcmp(a, "\xAB\xAB\xAB\xAB", 4) == 0, "first block is not overwritten by the second");
    ::operator delete(a);
    void* c = ::operator new(24);
    Expect(c == a, "freelist reuses the size class");
    ::operator delete(b);
    ::operator delete(c);

#if defined(__APPLE__)
    // Metal frees C++ memory with free(), not operator delete.
    void* sysfree = ::operator new(48);
    std::memset(sysfree, 0x5A, 48);
    std::free(sysfree);
    void* again = ::operator new(48);
    Expect(again == sysfree, "free() returns the block to the freelist");
    ::operator delete(again);
#endif

    constexpr int kN = 200;
    void* many[kN];
    for (int i = 0; i < kN; i++) {
        size_t n = 16 + size_t(i % 50);
        many[i] = ::operator new(n);
        std::memset(many[i], uint8_t(i), n);
    }
    for (int i = 0; i < kN; i += 2) ::operator delete(many[i]);
    for (int i = 0; i < kN; i += 2) {
        size_t n = 16 + size_t(i % 50);
        many[i] = ::operator new(n);
        std::memset(many[i], uint8_t(i), n);
    }
    bool intact = true;
    for (int i = 1; i < kN; i += 2) {
        auto* bytes = static_cast<uint8_t*>(many[i]);
        if (bytes[0] != uint8_t(i) || bytes[15 + (i % 50)] != uint8_t(i)) intact = false;
    }
    Expect(intact, "churn does not overlap live blocks");
    for (int i = 0; i < kN; i++) ::operator delete(many[i]);

    std::vector<int> live;
    live.push_back(1);
    live.push_back(2);
    live.push_back(3);
    Expect(in_arena(live.data()), "vector buffer is in the arena");
    live.clear();
    live.shrink_to_fit();

    det::ArenaSetActive(false);
    std::vector<int> v(8, 3);
    Expect(!in_arena(v.data()), "vector stays on the system heap while off");
    v.clear();
    v.shrink_to_fit();

    size_t used = det::ArenaUsed();
    std::vector<uint8_t> image(used);
    size_t got = 0;
    Expect(det::ArenaExport(image.data(), image.size(), &got) && got == used, "export");
    det::ArenaSetActive(true);
    void* d = ::operator new(64);
    std::memset(d, 0xCD, 64);
    Expect(det::ArenaImport(image.data(), got), "import");
    Expect(det::ArenaActive(), "import turns the arena on");
    Expect(det::ArenaUsed() == used, "bump restored");
    ::operator delete(d);

    if (g_fails) {
        std::fprintf(stderr, "test_arena: %d failed\n", g_fails);
        return 1;
    }
    std::printf("test_arena: PASS\n");
    return 0;
}
