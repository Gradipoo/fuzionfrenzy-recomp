#pragma once
// Length-prefixed blobs for the snapshot sections. The bytes are a raw image
// of a host container. Each host loads only snapshots it wrote.
#include <cstdint>
#include <cstring>
#include <vector>

inline void SnapPut(std::vector<uint8_t>& o, const void* p, size_t n) {
    uint64_t len = n;
    const auto* b = reinterpret_cast<const uint8_t*>(&len);
    o.insert(o.end(), b, b + sizeof len);
    const auto* c = static_cast<const uint8_t*>(p);
    o.insert(o.end(), c, c + n);
}

inline bool SnapGet(const uint8_t*& p, const uint8_t* end, void* dst, size_t n) {
    if (p + sizeof(uint64_t) > end) return false;
    uint64_t len = 0;
    std::memcpy(&len, p, sizeof len);
    p += sizeof len;
    if (len != n || p + n > end) return false;
    std::memcpy(dst, p, n);
    p += n;
    return true;
}
