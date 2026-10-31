#pragma once
#include <cstddef>
#include <cstdint>

namespace det {

// Fixed-address heap for snapshot runs. Off unless a snapshot env is set.
// Operator new forwards here while it is on. The bump and the freelist live
// outside the mapping, so a bad block cannot overwrite them. The saved image
// is that bookkeeping followed by the bytes from the base through the bump.
// On macOS the mapping is a registered malloc zone: Metal and libc++ free
// C++ memory with free(), and libmalloc aborts if the pointer is unknown.
bool ArenaMap();
void ArenaSetActive(bool on);
// This thread's new/malloc miss the arena until the matching resume. Other
// threads keep allocating into it. A process-wide pause races a guest thread
// that is building a wait, and the resumed stack then faults on that pointer.
void ArenaBypass(bool on);
bool ArenaActive();
void* ArenaAlloc(size_t n, size_t align);
void ArenaFree(void* p);
uint8_t* ArenaBase();
size_t ArenaUsed();
size_t ArenaCapacity();
// Copy [base, used) out, or install an image and turn the arena on.
bool ArenaExport(uint8_t* dst, size_t cap, size_t* used);
bool ArenaImport(const uint8_t* src, size_t n);

}  // namespace det
