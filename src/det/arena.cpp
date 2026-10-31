#include "det/arena.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <execinfo.h>
#include <new>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#if defined(__APPLE__)
#include <malloc/malloc.h>
#endif

namespace det {
namespace {

// 0x7100000000 sits inside a 4 GB guest reservation that macOS hands out at
// 0x7000000000 once ASLR is off. Keep the heap above that window.
constexpr uintptr_t kHint = 0x8000000000ull;
constexpr size_t kSize = 256ull << 20;
constexpr uint32_t kMagic = 0xA11C0001u;
constexpr uint64_t kImageMagic = 0x4152454E41310000ull;  // ARENA1
constexpr uint64_t kEmpty = ~uint64_t{0};  // freelist link; 0 is a real block
constexpr int kClasses = 28;  // 32 B .. 4 GiB, capped by the mapping
constexpr size_t kHdr = 16;

// Bookkeeping stays out of the mapping. A block header at offset 0 is the
// first allocation, not this struct.
struct Meta {
    uint64_t magic;
    uint64_t bump;
    uint64_t free_head[kClasses];
};

struct Block {
    uint32_t magic;
    uint32_t cls;
    uint64_t next;
};

Meta g_meta{};
std::atomic<uint32_t> g_lock{0};
uint8_t* g_base = nullptr;
size_t g_cap = 0;
bool g_mapped = false;
bool g_active = false;
thread_local int g_bypass = 0;

int ClassFor(size_t bytes) {
    size_t need = bytes < 32 ? 32 : bytes;
    int c = 0;
    size_t s = 32;
    while (s < need && c + 1 < kClasses) {
        s <<= 1;
        c++;
    }
    return c;
}

size_t ClassSize(int c) { return size_t(32) << c; }

void Lock() {
    uint32_t expected = 0;
    while (!g_lock.compare_exchange_weak(expected, 1, std::memory_order_acquire)) expected = 0;
}

void Unlock() { g_lock.store(0, std::memory_order_release); }

bool MapAt(uintptr_t hint) {
#if defined(_WIN32)
    void* p = VirtualAlloc(reinterpret_cast<void*>(hint), kSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!p) return false;
#else
    void* p = mmap(reinterpret_cast<void*>(hint), kSize, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p == MAP_FAILED) return false;
#endif
    if (reinterpret_cast<uintptr_t>(p) != hint) return false;
    g_base = static_cast<uint8_t*>(p);
    g_cap = kSize;
    g_meta = Meta{};
    g_meta.magic = kImageMagic;
    g_meta.bump = 0;
    for (int i = 0; i < kClasses; i++) g_meta.free_head[i] = kEmpty;
    g_active = false;
    g_lock.store(0, std::memory_order_relaxed);
    g_mapped = true;
    return true;
}

}  // namespace

#if defined(__APPLE__)
void RegisterZone();
#endif

bool ArenaMap() {
    if (g_mapped) return true;
    if (!MapAt(kHint)) {
        std::fprintf(stderr, "det: arena map at 0x%llx failed\n", (unsigned long long)kHint);
        return false;
    }
    std::fprintf(stderr, "det: arena %zu MiB at %p\n", kSize >> 20, static_cast<void*>(g_base));
#if defined(__APPLE__)
    RegisterZone();
#endif
    return true;
}

void ArenaSetActive(bool on) {
    if (on && !g_mapped && !ArenaMap()) return;
    if (g_mapped) g_active = on;
}

void ArenaBypass(bool on) {
    if (on) g_bypass++;
    else if (g_bypass) g_bypass--;
}

bool ArenaActive() { return g_mapped && g_active && g_bypass == 0; }

uint8_t* ArenaBase() { return g_base; }
size_t ArenaUsed() { return g_mapped ? sizeof(Meta) + size_t(g_meta.bump) : 0; }
size_t ArenaCapacity() { return g_cap; }

void* ArenaAlloc(size_t n, size_t align) {
    if (!g_mapped || !n || g_meta.bump > g_cap) return nullptr;
    if (align < 16 || (align & (align - 1)) != 0) align = 16;
    // Round the payload so the next header cannot start inside it.
    size_t payload = (n + 15) & ~size_t(15);
    if (payload < n) return nullptr;
    size_t bytes = payload + kHdr;
    int cls = ClassFor(bytes);
    size_t block = ClassSize(cls);
    if (block < bytes) block = bytes;
    Lock();
    void* user = nullptr;
    if (align <= 16 && g_meta.free_head[cls] != kEmpty) {
        uint64_t off = g_meta.free_head[cls];
        if (off + block <= g_meta.bump && off + sizeof(Block) <= g_cap) {
            auto* b = reinterpret_cast<Block*>(g_base + off);
            g_meta.free_head[cls] = b->next;
            b->magic = kMagic;
            b->cls = uint32_t(cls);
            b->next = n;  // live blocks store the user size here
            user = reinterpret_cast<uint8_t*>(b) + kHdr;
        } else {
            g_meta.free_head[cls] = kEmpty;
        }
    }
    if (!user) {
        // The header is the 16 bytes before the user pointer. Starting the
        // user at the previous bump put that header on top of the last block.
        // The reservation is the whole size class, so a reused block cannot
        // share bytes with the block that was bumped after it.
        uint64_t user_off = (g_meta.bump + kHdr + align - 1) & ~uint64_t(align - 1);
        uint64_t hdr_off = user_off - kHdr;
        uint64_t end = hdr_off + block;
        if (user_off + payload > end) end = user_off + payload;
        end = (end + 15) & ~uint64_t(15);
        if (hdr_off >= g_meta.bump && user_off == hdr_off + kHdr && end <= g_cap) {
            auto* b = reinterpret_cast<Block*>(g_base + hdr_off);
            b->magic = kMagic;
            b->cls = uint32_t(cls);
            b->next = n;
            g_meta.bump = end;
            user = g_base + user_off;
        }
    }
    Unlock();
    return user;
}

void ArenaFree(void* p) {
    if (!p || !g_base) return;
    auto* u = static_cast<uint8_t*>(p);
    if (u < g_base + kHdr || u >= g_base + g_cap) return;
    auto* b = reinterpret_cast<Block*>(u - kHdr);
    if (b->magic != kMagic) return;
    int cls = int(b->cls);
    if (cls < 0 || cls >= kClasses) return;
    Lock();
    b->next = g_meta.free_head[cls];
    g_meta.free_head[cls] = uint64_t(reinterpret_cast<uint8_t*>(b) - g_base);
    b->magic = 0;
    Unlock();
}

#if defined(__APPLE__)
// libmalloc looks the pointer up in every registered zone before it aborts.
// Claiming the whole mapping is required: a false negative aborts the process.
bool Owns(const void* p) {
    if (!p || !g_base) return false;
    auto u = reinterpret_cast<uintptr_t>(p);
    auto b = reinterpret_cast<uintptr_t>(g_base);
    return u >= b && u < b + g_cap;
}

size_t ZoneSize(malloc_zone_t*, const void* p) {
    if (!Owns(p)) return 0;
    auto* u = static_cast<const uint8_t*>(p);
    if (u < g_base + kHdr) return 0;
    auto* b = reinterpret_cast<const Block*>(u - kHdr);
    if (b->magic != kMagic) return 0;
    return size_t(b->next);
}

boolean_t ZoneClaim(malloc_zone_t*, void* p) { return Owns(p) ? 1 : 0; }

void ZoneFree(malloc_zone_t*, void* p) { ArenaFree(p); }

void ZoneFreeSized(malloc_zone_t*, void* p, size_t) { ArenaFree(p); }

void* ZoneMalloc(malloc_zone_t*, size_t n) { return ArenaAlloc(n ? n : 1, 16); }

void* ZoneCalloc(malloc_zone_t*, size_t count, size_t size) {
    if (count && size > SIZE_MAX / count) return nullptr;
    size_t n = count * size;
    void* p = ArenaAlloc(n ? n : 1, 16);
    if (p && n) std::memset(p, 0, n);
    return p;
}

void* ZoneMemalign(malloc_zone_t*, size_t align, size_t n) { return ArenaAlloc(n ? n : 1, align); }

void* ZoneValloc(malloc_zone_t*, size_t n) { return ArenaAlloc(n ? n : 1, 4096); }

void* ZoneRealloc(malloc_zone_t*, void* p, size_t n) {
    if (!p) return ArenaAlloc(n ? n : 1, 16);
    if (!n) {
        ArenaFree(p);
        return nullptr;
    }
    void* q = ArenaAlloc(n, 16);
    if (!q) return nullptr;
    size_t old = ZoneSize(nullptr, p);
    std::memcpy(q, p, old < n ? old : n);
    ArenaFree(p);
    return q;
}

void ZoneDestroy(malloc_zone_t*) {}

kern_return_t ZoneEnum(task_t, void*, unsigned, vm_address_t, memory_reader_t, vm_range_recorder_t) {
    return KERN_SUCCESS;
}
size_t ZoneGood(malloc_zone_t*, size_t n) { return n; }
boolean_t ZoneCheck(malloc_zone_t*) { return 1; }
void ZonePrint(malloc_zone_t*, boolean_t) {}
void ZoneLog(malloc_zone_t*, void*) {}
void ZoneLock(malloc_zone_t*) {}
void ZoneUnlock(malloc_zone_t*) {}
void ZoneStats(malloc_zone_t*, malloc_statistics_t* s) {
    if (s) *s = {};
}
boolean_t ZoneLocked(malloc_zone_t*) { return 0; }

malloc_zone_t g_zone{};
malloc_introspection_t g_intro{};

void RegisterZone() {
    static bool once = false;
    if (once) return;
    once = true;
    g_intro.enumerator = ZoneEnum;
    g_intro.good_size = ZoneGood;
    g_intro.check = ZoneCheck;
    g_intro.print = ZonePrint;
    g_intro.log = ZoneLog;
    g_intro.force_lock = ZoneLock;
    g_intro.force_unlock = ZoneUnlock;
    g_intro.statistics = ZoneStats;
    g_intro.zone_locked = ZoneLocked;
    g_zone.size = ZoneSize;
    g_zone.malloc = ZoneMalloc;
    g_zone.calloc = ZoneCalloc;
    g_zone.valloc = ZoneValloc;
    g_zone.free = ZoneFree;
    g_zone.realloc = ZoneRealloc;
    g_zone.destroy = ZoneDestroy;
    g_zone.zone_name = "fuzion-snapshot";
    g_zone.introspect = &g_intro;
    g_zone.version = 10;
    g_zone.memalign = ZoneMemalign;
    g_zone.free_definite_size = ZoneFreeSized;
    g_zone.claimed_address = ZoneClaim;
    malloc_zone_register(&g_zone);
}
#endif

bool ArenaExport(uint8_t* dst, size_t cap, size_t* used) {
    if (!g_mapped || !dst) return false;
    Lock();
    size_t n = sizeof(Meta) + size_t(g_meta.bump);
    bool ok = n <= cap && g_meta.bump <= g_cap;
    if (ok) {
        std::memcpy(dst, &g_meta, sizeof(Meta));
        if (g_meta.bump) std::memcpy(dst + sizeof(Meta), g_base, size_t(g_meta.bump));
        if (used) *used = n;
    }
    Unlock();
    return ok;
}

bool ArenaImport(const uint8_t* src, size_t n) {
    if (!ArenaMap() || !src || n < sizeof(Meta)) return false;
    Meta meta{};
    std::memcpy(&meta, src, sizeof meta);
    if (meta.magic != kImageMagic || meta.bump > g_cap || sizeof(Meta) + meta.bump != n) return false;
    if (meta.bump) std::memcpy(g_base, src + sizeof(Meta), size_t(meta.bump));
    g_meta = meta;
    g_lock.store(0, std::memory_order_relaxed);
    g_active = true;
    return true;
}

}  // namespace det

void* operator new(std::size_t n) {
    if (det::ArenaActive()) {
        if (void* p = det::ArenaAlloc(n, 16)) return p;
        std::fprintf(stderr, "det: arena miss %zu used %zu\n", n, det::ArenaUsed());
        void* frames[8];
        int nf = backtrace(frames, 8);
        backtrace_symbols_fd(frames, nf, 2);
    }
    if (void* p = std::malloc(n ? n : 1)) return p;
    std::fprintf(stderr, "det: bad_alloc %zu arena %d used %zu cap %zu\n", n, int(det::ArenaActive()), det::ArenaUsed(),
                 det::ArenaCapacity());
    void* frames[12];
    int nf = backtrace(frames, 12);
    backtrace_symbols_fd(frames, nf, 2);
    std::fflush(stderr);
    throw std::bad_alloc();
}

void* operator new(std::size_t n, std::align_val_t al) {
    size_t align = size_t(al);
    if (align < sizeof(void*)) align = sizeof(void*);
    if (det::ArenaActive()) {
        if (void* p = det::ArenaAlloc(n, align)) return p;
    }
    void* p = nullptr;
#if defined(_WIN32)
    p = _aligned_malloc(n ? n : 1, align);
#else
    if (posix_memalign(&p, align, n ? n : 1) != 0) p = nullptr;
#endif
    if (p) return p;
    std::fprintf(stderr, "det: bad_alloc align %zu %zu arena %d used %zu\n", n, align, int(det::ArenaActive()),
                 det::ArenaUsed());
    throw std::bad_alloc();
}

void operator delete(void* p) noexcept {
    if (!p) return;
    uint8_t* b = det::ArenaBase();
    size_t cap = det::ArenaCapacity();
    if (b && p >= b && p < b + cap) {
        det::ArenaFree(p);
        return;
    }
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept { operator delete(p); }
void operator delete(void* p, std::align_val_t) noexcept {
    if (!p) return;
    uint8_t* b = det::ArenaBase();
    size_t cap = det::ArenaCapacity();
    if (b && p >= b && p < b + cap) {
        det::ArenaFree(p);
        return;
    }
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}
void operator delete(void* p, std::size_t, std::align_val_t al) noexcept { operator delete(p, al); }

void* operator new[](std::size_t n) { return operator new(n); }
void* operator new[](std::size_t n, std::align_val_t al) { return operator new(n, al); }
void operator delete[](void* p) noexcept { operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { operator delete(p); }
void operator delete[](void* p, std::align_val_t al) noexcept { operator delete(p, al); }
void operator delete[](void* p, std::size_t, std::align_val_t al) noexcept { operator delete(p, al); }

#if defined(__APPLE__)
// libc++ allocates with __builtin_operator_new, which calls malloc rather than
// the replaceable operator new. Without this, a std::vector's buffer is on
// the system heap and a resumed thread faults on that dangling pointer.
// The default zone's function pointers are the real allocator; calling them
// does not re-enter these symbols.
static malloc_zone_t* SysZone() {
    static malloc_zone_t* z = malloc_default_zone();
    return z;
}

// visibility default: the game is built with -fvisibility=hidden, and a hidden
// malloc is not what the other objects call. libc++ allocates with
// __builtin_operator_new, which calls this symbol rather than operator new.
__attribute__((visibility("default"), used))
extern "C" void* malloc(size_t n) {
    if (det::ArenaActive()) {
        if (void* p = det::ArenaAlloc(n ? n : 1, 16)) return p;
    }
    return SysZone()->malloc(SysZone(), n ? n : 1);
}

__attribute__((visibility("default"), used))
extern "C" void free(void* p) {
    if (!p) return;
    uint8_t* b = det::ArenaBase();
    size_t cap = det::ArenaCapacity();
    if (b && p >= b && p < b + cap) {
        det::ArenaFree(p);
        return;
    }
    SysZone()->free(SysZone(), p);
}

__attribute__((visibility("default"), used))
extern "C" void* calloc(size_t count, size_t size) {
    if (count && size > SIZE_MAX / count) return nullptr;
    size_t n = count * size;
    if (det::ArenaActive()) {
        void* p = det::ArenaAlloc(n ? n : 1, 16);
        if (p) {
            if (n) std::memset(p, 0, n);
            return p;
        }
    }
    return SysZone()->calloc(SysZone(), count, size);
}

__attribute__((visibility("default"), used))
extern "C" void* realloc(void* p, size_t n) {
    uint8_t* b = det::ArenaBase();
    size_t cap = det::ArenaCapacity();
    bool ours = b && p && p >= b && p < b + cap;
    if (!p) return malloc(n);
    if (!n) {
        free(p);
        return nullptr;
    }
    if (ours || det::ArenaActive()) {
        void* q = det::ArenaActive() ? det::ArenaAlloc(n, 16) : nullptr;
        if (!q) q = SysZone()->malloc(SysZone(), n);
        if (!q) return nullptr;
        size_t old = n;
        if (ours) {
            auto* u = static_cast<uint8_t*>(p);
            if (u >= b + 16) {
                auto* blk = reinterpret_cast<const uint8_t*>(u - 16);
                uint64_t stored = 0;
                std::memcpy(&stored, blk + 8, 8);  // live next field is the user size
                if (stored && stored < old) old = size_t(stored);
            }
        }
        std::memcpy(q, p, old < n ? old : n);
        free(p);
        return q;
    }
    return SysZone()->realloc(SysZone(), p, n);
}
#endif
