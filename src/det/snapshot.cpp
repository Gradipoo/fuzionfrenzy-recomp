// Fixed-step save and resume. Guest calls are host calls, so a snapshot is the
// host stacks of threads blocked in the harness, plus guest RAM and the host
// heap those stacks point at. sigsetjmp runs at the start of OnPresented and
// at the start of Park, WaitTicks, and AcquireTurn, before those functions
// take a lock. Resume longjmps back to that point and the wait is taken on
// the new process's condition variable.
//
// ASLR is disabled only when a snapshot env is set. The arena and the fixed
// stacks stay off for a normal launch.
#include "det/arena.h"
#include "det/snap_blob.h"

#include "core/determinism.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <setjmp.h>

#if !defined(_WIN32)
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#if defined(__linux__)
#include <sys/personality.h>
#ifndef ADDR_NO_RANDOMIZE
#define ADDR_NO_RANDOMIZE 0x0040000
#endif
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <spawn.h>
extern char** environ;
#endif

namespace d3d {
void SnapshotCapture(std::vector<uint8_t>& out);
bool SnapshotInstall(const uint8_t* p, size_t n);
void SnapshotCaptureRing(std::vector<uint8_t>& out);
bool SnapshotInstallRing(const uint8_t* p, size_t n);
}
namespace xapi {
void SnapshotCapture(std::vector<uint8_t>& out);
bool SnapshotInstall(const uint8_t* p, size_t n);
}
namespace dsound {
void SnapshotCapture(std::vector<uint8_t>& out);
bool SnapshotInstall(const uint8_t* p, size_t n);
}

void xbe_arm_host_fp();

namespace {

// Above a 4 GB guest reservation that starts at 0x7000000000. A stack planted
// on the null-guard boundary (0x7000000000) maps, then the guest never presents.
constexpr uintptr_t kStackBase = 0x8200000000ull;
constexpr size_t kStackSize = 64ull << 20;
// pthread starts at the high end of the stack it is given. The extra gap sits
// above the saved image so that frame does not overwrite the restored frames.
constexpr size_t kStartup = 64ull << 10;
constexpr size_t kStackStride = kStackSize + kStartup;
constexpr int kSlots = 16;
constexpr uint32_t kVersion = 2;

enum Tag : uint32_t {
    kPhys = 1, kRam = 2, kArena = 3, kHarness = 4, kObjects = 5, kXinput = 6, kDsound = 7, kD3d = 8, kThreads = 9,
    kRanges = 10, kRing = 11, kLinks = 12
};

struct Slot {
    uintptr_t lo = 0;
    uintptr_t hi = 0;
    int used = 0;
};

struct Live {
    int kind = 0;
    int slot = -1;
    uintptr_t lo = 0, hi = 0, sp = 0;
    sigjmp_buf jb{};
    uint64_t cookie = 0;  // Apple arm64 thread cookie that hides fp/lr/sp
    det::TlsImage tls{};
    std::vector<uint8_t> stack;
    bool have = false;
};

Slot g_slots[kSlots];
Live g_present;
Live g_block[kSlots];
std::atomic<int> g_entered{0};
uint64_t g_want_base = 0;
uint64_t g_want_slide = 0;
bool g_load = false;
std::string g_hdd;

namespace fs = std::filesystem;

void ScrubApple(const fs::path& dir) {
    std::error_code ec;
    std::vector<fs::path> junk;
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
    for (; it != end; it.increment(ec)) {
        std::string name = it->path().filename().string();
        if (name.rfind("._", 0) == 0 || name == ".DS_Store") junk.push_back(it->path());
    }
    for (const fs::path& p : junk) fs::remove(p, ec);
}

// The resumed guest opens files the frame had already created. The boot image
// does not have them, so the open fails and the next handle is one slot early.
void CopyHdd(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    if (!fs::is_directory(from, ec)) return;
    fs::create_directories(to, ec);
    std::vector<fs::path> gone, kids;
    for (fs::directory_iterator it(to, ec), end; it != end; it.increment(ec)) gone.push_back(it->path());
    for (const fs::path& p : gone) fs::remove_all(p, ec);
    for (fs::directory_iterator it(from, ec), end; it != end; it.increment(ec)) kids.push_back(it->path());
    for (const fs::path& p : kids)
        fs::copy(p, to / p.filename(), fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    ScrubApple(to);
}

// Snapshot scratch (the stack image, the RAM copy) must not land in the arena.
// Turning the arena off for every thread lets a guest that is entering a wait
// put its vector on the system heap. Resume then faults on that pointer.
struct Pause {
    Pause() { det::ArenaBypass(true); }
    ~Pause() { det::ArenaBypass(false); }
};

bool EnvSet(const char* name) {
    const char* v = std::getenv(name);
    return v && v[0] && std::strcmp(v, "0") != 0;
}

bool Saving() { return EnvSet("FUZION_SNAPSHOT_DIR"); }
bool Loading() { return EnvSet("FUZION_LOAD_SNAPSHOT"); }
bool Active() { return Saving() || Loading(); }

uintptr_t SlideOf() {
    return reinterpret_cast<uintptr_t>(&SlideOf);
}

const char* SelfPath(char* buf, size_t n) {
#if defined(__APPLE__)
    uint32_t sz = uint32_t(n);
    if (_NSGetExecutablePath(buf, &sz) != 0) return nullptr;
    return buf;
#elif defined(__linux__)
    ssize_t got = ::readlink("/proc/self/exe", buf, n - 1);
    if (got <= 0) return nullptr;
    buf[got] = 0;
    return buf;
#else
    (void)buf;
    (void)n;
    return nullptr;
#endif
}

void ParseFrames(uint64_t* out, int* n) {
    *n = 0;
    const char* s = std::getenv("FUZION_SNAPSHOT_FRAMES");
    if (!s || !*s) return;
    while (*s && *n < 32) {
        char* end = nullptr;
        unsigned long long v = std::strtoull(s, &end, 10);
        if (end == s) break;
        out[(*n)++] = v;
        s = end;
        if (*s == ',') s++;
    }
}

bool WantFrame(uint64_t frame) {
    uint64_t frames[32];
    int n = 0;
    ParseFrames(frames, &n);
    if (!n) return false;
    for (int i = 0; i < n; i++)
        if (frames[i] == frame) return true;
    return false;
}

Slot* SlotOf(uintptr_t a) {
    for (int i = 0; i < kSlots; i++) {
        if (!g_slots[i].used) continue;
        if (a >= g_slots[i].lo && a < g_slots[i].hi) return &g_slots[i];
    }
    return nullptr;
}

// Darwin's sigjmp_buf does not store a raw stack pointer (the registers are
// scrambled), so the copy window comes from the live SP. siglongjmp restores
// the pointer it saved, which is a few frames from this read.
uintptr_t CurrentSp() {
    uintptr_t sp = 0;
#if defined(__aarch64__)
    __asm__ volatile("mov %0, sp" : "=r"(sp));
#elif defined(__x86_64__)
    __asm__ volatile("mov %%rsp, %0" : "=r"(sp));
#else
    volatile char local = 0;
    sp = reinterpret_cast<uintptr_t>(const_cast<volatile char*>(&local));
#endif
    return sp;
}

// macOS arm64 __longjmp xors fp, lr, and sp with a per-thread cookie at
// TPIDRRO_EL0+0x38. A resumed process has a new cookie, so the saved words
// are retargeted before siglongjmp. The pointers themselves are not signed
// for an arm64 (not arm64e) binary.
uint64_t JmpCookie() {
#if defined(__APPLE__) && defined(__aarch64__)
    uintptr_t base = 0;
    __asm__ volatile("mrs %0, TPIDRRO_EL0" : "=r"(base));
    return *reinterpret_cast<const uint64_t*>(base + 0x38);
#else
    return 0;
#endif
}

void RetargetJmp(sigjmp_buf jb, uint64_t saved) {
#if defined(__APPLE__) && defined(__aarch64__)
    if (!saved) return;
    uint64_t delta = saved ^ JmpCookie();
    auto* w = reinterpret_cast<uint64_t*>(jb);
    w[10] ^= delta;
    w[11] ^= delta;
    w[12] ^= delta;
#else
    (void)jb;
    (void)saved;
#endif
}

void Fill(Live& live, int kind, sigjmp_buf jb) {
    uintptr_t sp = CurrentSp();
    Slot* s = SlotOf(sp);
    if (!s) return;
    if (sp < s->lo || sp >= s->hi) {
        std::fprintf(stderr, "det: snapshot sp 0x%llx outside its stack\n", (unsigned long long)sp);
        return;
    }
    // The image is taken here, at the setjmp, not later. A later copy includes
    // the wait that resume starts over. The buffer stays off the arena so the
    // copy does not move a later guest allocation.
    live.kind = kind;
    live.slot = int(s - g_slots);
    live.lo = s->lo;
    live.hi = s->hi;
    live.sp = sp;
    std::memcpy(&live.jb, jb, sizeof(sigjmp_buf));
    live.cookie = JmpCookie();
    det::ExportTls(live.tls);
    live.have = true;
    // Bypass the arena: the image buffer must not change later guest allocations.
    Pause pause;
    size_t n = size_t(live.hi - live.sp);
    live.stack.resize(n);
    std::memcpy(live.stack.data(), reinterpret_cast<const void*>(live.sp), n);
}

bool WriteSec(FILE* f, uint32_t tag, const void* p, size_t n) {
    uint32_t pad = 0;
    uint64_t len = n;
    if (std::fwrite(&tag, 4, 1, f) != 1) return false;
    if (std::fwrite(&pad, 4, 1, f) != 1) return false;
    if (std::fwrite(&len, 8, 1, f) != 1) return false;
    if (n && std::fwrite(p, 1, n, f) != n) return false;
    return true;
}

bool WriteRam(FILE* f) {
    Pause pause;
    std::vector<uint8_t> buf;
    struct Ctx {
        std::vector<uint8_t>* buf;
        uint8_t* base;
    } ctx{&buf, mem::Base()};
    mem::VisitCommitted(
        [](uint32_t va, uint32_t pages, void* user) {
            auto* c = static_cast<Ctx*>(user);
            uint32_t n = pages;
            const uint8_t* src = c->base + va;
            const uint8_t* vp = reinterpret_cast<const uint8_t*>(&va);
            const uint8_t* np = reinterpret_cast<const uint8_t*>(&n);
            c->buf->insert(c->buf->end(), vp, vp + 4);
            c->buf->insert(c->buf->end(), np, np + 4);
            c->buf->insert(c->buf->end(), src, src + size_t(pages) * mem::kPage);
        },
        &ctx);
    return WriteSec(f, kRam, buf.data(), buf.size());
}

bool WriteThreads(FILE* f) {
    Pause pause;
    std::vector<uint8_t> buf;
    auto put = [&](const void* p, size_t n) {
        const auto* b = static_cast<const uint8_t*>(p);
        buf.insert(buf.end(), b, b + n);
    };
    uint32_t count = 0;
    // A thread that has already exited still has the stack from its last wait.
    // Resuming that image runs the wait again and deadlocks the scheduler.
    auto keep = [](const Live& L) {
        return L.have && (L.kind == 1 || kernel::ThreadRegistered(L.tls.thread_id));
    };
    if (keep(g_present)) count++;
    for (const Live& b : g_block)
        if (keep(b)) count++;
    put(&count, 4);
    auto one = [&](const Live& L) {
        if (!keep(L)) return;
        uint32_t kind = uint32_t(L.kind), slot = uint32_t(L.slot);
        put(&kind, 4);
        put(&slot, 4);
        put(&L.lo, 8);
        put(&L.hi, 8);
        put(&L.sp, 8);
        put(&L.tls, sizeof L.tls);
        uint32_t jn = uint32_t(sizeof(sigjmp_buf));
        put(&jn, 4);
        put(&L.jb, jn);
        put(&L.cookie, 8);
        uint64_t sn = L.stack.size();
        put(&sn, 8);
        if (sn) put(L.stack.data(), size_t(sn));
    };
    one(g_present);
    for (const Live& b : g_block) one(b);
    return WriteSec(f, kThreads, buf.data(), buf.size());
}

bool WriteSnapshot(uint64_t frame) {
    const char* dir = std::getenv("FUZION_SNAPSHOT_DIR");
    if (!dir || !dir[0]) return false;
#if !defined(_WIN32)
    ::mkdir(dir, 0755);
#endif
    char path[1024], tmp[1100];
    std::snprintf(path, sizeof path, "%s/frame_%llu.ffsnap", dir, (unsigned long long)frame);
    std::snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE* f = std::fopen(tmp, "wb");
    if (!f) {
        std::fprintf(stderr, "det: snapshot open %s failed\n", tmp);
        return false;
    }
    char magic[8] = {'F', 'F', 'S', 'N', 'A', 'P', 0, 0};
    uint32_t version = kVersion, zero = 0;
    uint64_t base = reinterpret_cast<uint64_t>(mem::Base());
    uint64_t slide = SlideOf();
    std::fwrite(magic, 1, 8, f);
    std::fwrite(&version, 4, 1, f);
    std::fwrite(&zero, 4, 1, f);
    std::fwrite(&base, 8, 1, f);
    std::fwrite(&slide, 8, 1, f);
    std::fwrite(&frame, 8, 1, f);
    if (!g_present.have) {
        std::fprintf(stderr, "det: snapshot frame %llu has no presenter stack\n", (unsigned long long)frame);
        std::fclose(f);
        std::remove(tmp);
        return false;
    }
    bool ok = true;
    if (mem::Base()) ok = ok && WriteSec(f, kPhys, mem::Base() + mem::kContiguousBase, mem::kPhysicalSize);
    ok = ok && WriteRam(f);
    {
        Pause pause;
        size_t used = det::ArenaUsed();
        std::vector<uint8_t> arena(used);
        size_t got = 0;
        if (!det::ArenaExport(arena.data(), arena.size(), &got)) ok = false;
        else ok = ok && WriteSec(f, kArena, arena.data(), got);
        det::HarnessImage h{};
        det::ExportHarness(h);
        ok = ok && WriteSec(f, kHarness, &h, sizeof h);
        std::vector<uint8_t> blob;
        kernel::SnapshotCaptureObjects(blob);
        kernel::SnapshotCaptureThreads(blob);
        ok = ok && WriteSec(f, kObjects, blob.data(), blob.size());
        blob.clear();
        xapi::SnapshotCapture(blob);
        ok = ok && WriteSec(f, kXinput, blob.data(), blob.size());
        blob.clear();
        dsound::SnapshotCapture(blob);
        ok = ok && WriteSec(f, kDsound, blob.data(), blob.size());
        blob.clear();
        d3d::SnapshotCapture(blob);
        ok = ok && WriteSec(f, kD3d, blob.data(), blob.size());
        blob.clear();
        mem::SnapshotCaptureRanges(blob);
        ok = ok && WriteSec(f, kRanges, blob.data(), blob.size());
        blob.clear();
        d3d::SnapshotCaptureRing(blob);
        ok = ok && WriteSec(f, kRing, blob.data(), blob.size());
        blob.clear();
        kernel::SnapshotCaptureLinks(blob);
        ok = ok && WriteSec(f, kLinks, blob.data(), blob.size());
    }
    ok = ok && WriteThreads(f);
    if (std::fclose(f) != 0) ok = false;
    if (!ok || std::rename(tmp, path) != 0) {
        std::fprintf(stderr, "det: snapshot write %s failed\n", path);
        std::remove(tmp);
        return false;
    }
        std::fprintf(stderr, "det: snapshot frame %llu -> %s (arena %zu bytes)\n", (unsigned long long)frame, path,
                     det::ArenaUsed());
    if (!g_hdd.empty()) {
        fs::path hdd = fs::path(dir) / ("hdd_" + std::to_string(frame));
        CopyHdd(g_hdd, hdd);
    }
    return true;
}

bool ReadSec(FILE* f, uint32_t* tag, std::vector<uint8_t>& body) {
    uint32_t pad = 0;
    uint64_t len = 0;
    if (std::fread(tag, 4, 1, f) != 1) return false;
    if (std::fread(&pad, 4, 1, f) != 1) return false;
    if (std::fread(&len, 8, 1, f) != 1) return false;
    if (len > (1ull << 32)) return false;
    body.resize(size_t(len));
    if (len && std::fread(body.data(), 1, size_t(len), f) != size_t(len)) return false;
    return true;
}

bool RestoreRam(const uint8_t* p, size_t n) {
    size_t i = 0;
    while (i + 8 <= n) {
        uint32_t va = 0, pages = 0;
        std::memcpy(&va, p + i, 4);
        std::memcpy(&pages, p + i + 4, 4);
        i += 8;
        size_t bytes = size_t(pages) * mem::kPage;
        if (i + bytes > n) return false;
        if (!mem::Commit(va, uint32_t(bytes))) return false;
        std::memcpy(mem::Base() + va, p + i, bytes);
        i += bytes;
    }
    return i == n;
}

bool MapStack(int slot, uintptr_t lo, uintptr_t hi) {
    if (slot < 0 || slot >= kSlots) return false;
    size_t n = size_t(hi - lo) + kStartup;
    if (g_slots[slot].used && g_slots[slot].lo == lo) return true;
#if defined(_WIN32)
    (void)n;
    return false;
#else
    void* p = mmap(reinterpret_cast<void*>(lo), n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p == MAP_FAILED || reinterpret_cast<uintptr_t>(p) != lo) {
        std::fprintf(stderr, "det: snapshot stack map %d at 0x%llx failed\n", slot, (unsigned long long)lo);
        return false;
    }
    g_slots[slot] = {lo, hi, 1};
    return true;
#endif
}

struct ResumeArg {
    Live* live;
    int blockers;
};

void* ResumeEntry(void* raw) {
    auto* arg = static_cast<ResumeArg*>(raw);
    Live* live = arg->live;
    xbe_arm_host_fp();
    det::ImportTls(live->tls);
    det::ReregisterTls();
    kernel::BindCurrent(live->tls.thread_id);
    sigjmp_buf jb;
    std::memcpy(&jb, &live->jb, sizeof jb);
    RetargetJmp(jb, live->cookie);
    // The longjmp returns into the saved xbe_block_snap / xbe_present_snap.
    // That copy counts blockers once they are back on the restored stack.
    siglongjmp(jb, 1);
    return nullptr;
}

bool SpawnOn(Live& live) {
    if (!MapStack(live.slot, live.lo, live.hi)) return false;
    if (live.stack.size() != size_t(live.hi - (live.sp > 2048 ? live.sp - 2048 : live.lo)) && live.stack.size() != size_t(live.hi - live.lo)) {
        // The image is [from, hi). Place it so the high end sits on hi.
    }
    if (live.stack.size() > size_t(live.hi - live.lo)) return false;
    std::memcpy(reinterpret_cast<void*>(live.hi - live.stack.size()), live.stack.data(), live.stack.size());
#if defined(_WIN32)
    return false;
#else
    auto* arg = new ResumeArg{&live, 0};
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setguardsize(&attr, 0);
    pthread_attr_setstack(&attr, reinterpret_cast<void*>(live.lo), size_t(live.hi - live.lo) + kStartup);
    pthread_t th;
    int rc = pthread_create(&th, &attr, ResumeEntry, arg);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        std::fprintf(stderr, "det: snapshot pthread_create failed (%d)\n", rc);
        return false;
    }
    pthread_detach(th);
    return true;
#endif
}

bool LoadThreads(const uint8_t* p, size_t n, std::vector<Live>& blockers, Live& present) {
    if (n < 4) return false;
    uint32_t count = 0;
    std::memcpy(&count, p, 4);
    size_t i = 4;
    auto need = [&](size_t k) {
        if (i + k > n) return false;
        return true;
    };
    for (uint32_t c = 0; c < count; c++) {
        if (!need(4 + 4 + 8 + 8 + 8 + sizeof(det::TlsImage) + 4)) return false;
        Live L;
        uint32_t kind = 0, slot = 0, jn = 0;
        std::memcpy(&kind, p + i, 4);
        i += 4;
        std::memcpy(&slot, p + i, 4);
        i += 4;
        std::memcpy(&L.lo, p + i, 8);
        i += 8;
        std::memcpy(&L.hi, p + i, 8);
        i += 8;
        std::memcpy(&L.sp, p + i, 8);
        i += 8;
        std::memcpy(&L.tls, p + i, sizeof L.tls);
        i += sizeof L.tls;
        std::memcpy(&jn, p + i, 4);
        i += 4;
        if (jn != sizeof(sigjmp_buf) || !need(jn + 8 + 8)) return false;
        std::memcpy(&L.jb, p + i, jn);
        i += jn;
        std::memcpy(&L.cookie, p + i, 8);
        i += 8;
        uint64_t sn = 0;
        std::memcpy(&sn, p + i, 8);
        i += 8;
        if (!need(size_t(sn))) return false;
        L.stack.assign(p + i, p + i + size_t(sn));
        i += size_t(sn);
        L.kind = int(kind);
        L.slot = int(slot);
        L.have = true;
        if (kind == 1) present = std::move(L);
        else blockers.push_back(std::move(L));
    }
    return i == n && present.have;
}


}  // namespace

// Blocker and presenter images live for the rest of the process. Threads
// longjmp through these objects, so they must not move after spawn.
static std::vector<Live> g_loaded_blockers;
static Live g_loaded_present;

extern "C" void xbe_reexec(int argc, char** argv) {
#if defined(_WIN32)
    (void)argc;
    (void)argv;
    return;
#else
    if (!Active()) return;
    if (std::getenv("FUZION_ASLR_CHILD")) return;
#if defined(__linux__)
    int cur = personality(0xffffffff);
    if (cur >= 0 && (cur & ADDR_NO_RANDOMIZE)) return;
    setenv("FUZION_ASLR_CHILD", "1", 1);
    if (cur >= 0) personality(cur | ADDR_NO_RANDOMIZE);
    execv("/proc/self/exe", argv);
    std::perror("det: execv");
    _exit(127);
#elif defined(__APPLE__)
    char path[4096];
    if (!SelfPath(path, sizeof path)) return;
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, 0x0100);  // POSIX_SPAWN_DISABLE_ASLR
    std::vector<std::string> store;
    std::vector<char*> envp;
    for (char** e = environ; e && *e; ++e) store.emplace_back(*e);
    store.emplace_back("FUZION_ASLR_CHILD=1");
    for (auto& s : store) envp.push_back(s.data());
    envp.push_back(nullptr);
    pid_t pid = 0;
    int rc = posix_spawn(&pid, path, nullptr, &attr, argv, envp.data());
    posix_spawnattr_destroy(&attr);
    if (rc != 0) {
        std::fprintf(stderr, "det: posix_spawn ASLR off failed (%d)\n", rc);
        return;
    }
    int st = 0;
    waitpid(pid, &st, 0);
    if (WIFEXITED(st)) _exit(WEXITSTATUS(st));
    if (WIFSIGNALED(st)) {
        int sig = WTERMSIG(st);
        char b[64];
        int n = std::snprintf(b, sizeof b, "det: child died on signal %d\n", sig);
        if (n > 0) ::write(STDERR_FILENO, b, size_t(n));
        _exit(128 + sig);
    }
    _exit(1);
#else
    (void)argc;
    (void)argv;
#endif
#endif
}

// The load path has to own the guest window before SDL and Vulkan allocate.
// Init maps it with MAP_FIXED. If the heap has already grown into that 4 GB,
// the fixed map unmaps live blocks and the next vector in a resumed wait faults.
extern "C" void xbe_reserve_early(void) {
#if !defined(_WIN32)
    if (!Loading()) return;
    const char* path = std::getenv("FUZION_LOAD_SNAPSHOT");
    FILE* f = std::fopen(path, "rb");
    if (!f) return;
    char magic[8] = {};
    uint32_t version = 0, zero = 0;
    uint64_t base = 0;
    std::fread(magic, 1, 8, f);
    std::fread(&version, 4, 1, f);
    std::fread(&zero, 4, 1, f);
    std::fread(&base, 8, 1, f);
    std::fclose(f);
    if (std::memcmp(magic, "FFSNAP", 6) != 0 || version != kVersion || !base) return;
    void* p = mmap(reinterpret_cast<void*>(base), 1ull << 32, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p == MAP_FAILED || reinterpret_cast<uintptr_t>(p) != base) {
        std::fprintf(stderr, "det: early guest reserve at 0x%llx failed\n", (unsigned long long)base);
        return;
    }
#else
    (void)0;
#endif
}

extern "C" void xbe_note_hdd(const char* path) {
    g_hdd = path ? path : "";
}

extern "C" void xbe_prepare_load(void) {
    if (!Loading()) return;
    const char* path = std::getenv("FUZION_LOAD_SNAPSHOT");
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "det: FUZION_LOAD_SNAPSHOT %s did not open\n", path ? path : "");
        return;
    }
    char magic[8] = {};
    uint32_t version = 0, zero = 0;
    uint64_t base = 0, slide = 0, frame = 0;
    std::fread(magic, 1, 8, f);
    std::fread(&version, 4, 1, f);
    std::fread(&zero, 4, 1, f);
    std::fread(&base, 8, 1, f);
    std::fread(&slide, 8, 1, f);
    std::fread(&frame, 8, 1, f);
    std::fclose(f);
    if (std::memcmp(magic, "FFSNAP", 6) != 0 || version != kVersion) {
        std::fprintf(stderr, "det: %s is not a snapshot\n", path);
        return;
    }
    g_want_base = base;
    g_want_slide = slide;
    g_load = true;
    if (!g_hdd.empty()) {
        fs::path src = fs::path(path).parent_path() / ("hdd_" + std::to_string(frame));
        if (fs::is_directory(src)) {
            CopyHdd(src, g_hdd);
            std::fprintf(stderr, "det: restored hdd %s\n", src.string().c_str());
        }
    }
    mem::SetBaseHint(static_cast<uintptr_t>(base));
    std::fprintf(stderr, "det: load snapshot %s frame %llu base 0x%llx\n", path, (unsigned long long)frame,
                 (unsigned long long)base);
}

extern "C" void xbe_arena_on(void) {
    if (!Saving()) return;
    if (!det::ArenaMap()) return;
    det::ArenaSetActive(true);
}

extern "C" int xbe_hold_boot(void) { return Loading() ? 1 : 0; }

extern "C" int xbe_snapshot_stacks(void) { return Active() ? 1 : 0; }

extern "C" void* xbe_take_stack(size_t* bytes) {
#if defined(_WIN32)
    (void)bytes;
    return nullptr;
#else
    if (!Active()) return nullptr;
    for (int i = 0; i < kSlots; i++) {
        if (g_slots[i].used) continue;
        uintptr_t lo = kStackBase + uintptr_t(i) * kStackStride;
        void* p = mmap(reinterpret_cast<void*>(lo), kStackSize, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        if (p == MAP_FAILED || reinterpret_cast<uintptr_t>(p) != lo) {
            std::fprintf(stderr, "det: fixed stack %d map failed\n", i);
            return nullptr;
        }
        g_slots[i] = {lo, lo + kStackSize, 1};
        if (bytes) *bytes = kStackSize;
        return p;
    }
    return nullptr;
#endif
}

extern "C" void xbe_present_snap(void) {
    if (!Saving()) return;
    if (EnvSet("FUZION_SNAPSHOT_NOJMP")) return;
    sigjmp_buf jb;
    if (sigsetjmp(jb, 0) != 0) return;
    Fill(g_present, 1, jb);
    if (g_present.slot >= 0 && g_present.slot < kSlots) g_block[g_present.slot].have = false;
    uint64_t frame = det::FrameIndex();
    if (WantFrame(frame)) WriteSnapshot(frame);
}

extern "C" void xbe_block_snap(void) {
    // A resumed thread longjmps to the setjmp below. A later call, on the load
    // process, has nothing to record.
    if (!Saving()) return;
    if (EnvSet("FUZION_SNAPSHOT_NOJMP")) return;
    if (det::OnPacer()) return;
    sigjmp_buf jb;
    if (sigsetjmp(jb, 0) != 0) {
        g_entered.fetch_add(1, std::memory_order_release);
        return;
    }
    if (!Saving()) return;
    Slot* s = SlotOf(CurrentSp());
    if (!s) return;
    Fill(g_block[s - g_slots], 2, jb);
}

extern "C" void xbe_after_boot(void) {
    if (!Loading()) return;
    const char* path = std::getenv("FUZION_LOAD_SNAPSHOT");
    // Inline restore into static Live objects, then spawn. This is the body
    // that used to live in Restore(), without moving a Live after its address
    // was handed to a thread.
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "det: resume open failed\n");
        kernel::Shutdown(1);
    }
    char magic[8] = {};
    uint32_t version = 0, zero = 0;
    uint64_t base = 0, slide = 0, frame = 0;
    std::fread(magic, 1, 8, f);
    std::fread(&version, 4, 1, f);
    std::fread(&zero, 4, 1, f);
    std::fread(&base, 8, 1, f);
    std::fread(&slide, 8, 1, f);
    std::fread(&frame, 8, 1, f);
    if (std::memcmp(magic, "FFSNAP", 6) != 0 || version != kVersion || slide != SlideOf() ||
        reinterpret_cast<uint64_t>(mem::Base()) != base) {
        std::fprintf(stderr, "det: resume refused (file slide 0x%llx now 0x%llx, file base 0x%llx now %p, version %u)\n",
                     (unsigned long long)slide, (unsigned long long)SlideOf(), (unsigned long long)base,
                     static_cast<void*>(mem::Base()), version);
        std::fclose(f);
        kernel::Shutdown(1);
    }
    std::vector<uint8_t> phys, ram, arena, objects, xin, ds, d3, th, ranges, ring, links;
    det::HarnessImage harness{};
    bool got_h = false;
    uint32_t tag = 0;
    std::vector<uint8_t> body;
    while (ReadSec(f, &tag, body)) {
        if (tag == kPhys) phys.swap(body);
        else if (tag == kRam) ram.swap(body);
        else if (tag == kArena) arena.swap(body);
        else if (tag == kHarness && body.size() == sizeof harness) {
            std::memcpy(&harness, body.data(), sizeof harness);
            got_h = true;
        } else if (tag == kObjects) objects.swap(body);
        else if (tag == kXinput) xin.swap(body);
        else if (tag == kDsound) ds.swap(body);
        else if (tag == kD3d) d3.swap(body);
        else if (tag == kThreads) th.swap(body);
        else if (tag == kRanges) ranges.swap(body);
        else if (tag == kRing) ring.swap(body);
        else if (tag == kLinks) links.swap(body);
        body.clear();
    }
    std::fclose(f);
    std::fprintf(stderr, "det: resume sections read\n");
    if (!got_h || arena.empty() || th.empty() || phys.size() != mem::kPhysicalSize || !det::ArenaImport(arena.data(), arena.size()) ||
        !kernel::InstallSnapshotObjects(objects.data(), objects.size()) || !xapi::SnapshotInstall(xin.data(), xin.size()) ||
        !dsound::SnapshotInstall(ds.data(), ds.size()) || !d3d::SnapshotInstall(d3.data(), d3.size())) {
        std::fprintf(stderr, "det: resume sections failed\n");
        kernel::Shutdown(1);
    }
    std::fprintf(stderr, "det: resume objects installed\n");
    kernel::ReinitThreadSync();
    kernel::RebindSnapshotFiles();
    if (links.empty() || !kernel::SnapshotInstallLinks(links.data(), links.size())) {
        std::fprintf(stderr, "det: resume links failed\n");
        kernel::Shutdown(1);
    }
    std::memcpy(mem::Base() + mem::kContiguousBase, phys.data(), phys.size());
    if (!RestoreRam(ram.data(), ram.size())) {
        std::fprintf(stderr, "det: resume RAM failed\n");
        kernel::Shutdown(1);
    }
    std::fprintf(stderr, "det: resume RAM installed\n");
    if (!ranges.empty() && !mem::SnapshotInstallRanges(ranges.data(), ranges.size())) {
        std::fprintf(stderr, "det: resume ranges failed\n");
        kernel::Shutdown(1);
    }
    if (!ring.empty() && !d3d::SnapshotInstallRing(ring.data(), ring.size())) {
        std::fprintf(stderr, "det: resume header ring failed\n");
        kernel::Shutdown(1);
    }
    {
        Pause pause;
        det::ImportHarness(harness);
    }
    g_loaded_blockers.clear();
    g_loaded_present = Live{};
    if (!LoadThreads(th.data(), th.size(), g_loaded_blockers, g_loaded_present)) {
        std::fprintf(stderr, "det: resume threads failed\n");
        kernel::Shutdown(1);
    }
    std::fprintf(stderr, "det: resume threads loaded %zu\n", g_loaded_blockers.size());
    g_entered.store(0);
    g_load = true;
    // The restored presenter is already the pacer. A new thread must not
    // claim that role just because this process has not booted.
    det::KeepPacer();
    int need = 0;
    for (Live& b : g_loaded_blockers) {
        if (b.slot == g_loaded_present.slot) continue;
        if (!kernel::ThreadRegistered(b.tls.thread_id)) {
            std::fprintf(stderr, "det: resume skip thread %x (exited before the snapshot)\n", b.tls.thread_id);
            continue;
        }
        if (!SpawnOn(b)) kernel::Shutdown(1);
        need++;
    }
    for (int spin = 0; spin < 5000 && g_entered.load() < need; spin++) {
#if !defined(_WIN32)
        usleep(1000);
#endif
    }
    for (int spin = 0; spin < 5000 && det::BlockersWaiting() < need; spin++) {
#if !defined(_WIN32)
        usleep(1000);
#endif
    }
    std::fprintf(stderr, "det: resume frame %llu blockers %d entered %d waiting %d\n", (unsigned long long)frame, need,
                 g_entered.load(), det::BlockersWaiting());
    if (!SpawnOn(g_loaded_present)) kernel::Shutdown(1);
}
