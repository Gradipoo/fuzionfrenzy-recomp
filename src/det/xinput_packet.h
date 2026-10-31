#pragma once
// XInput packet change. Only report fields count. Padding after a bool is not
// a report field and is not stable across hosts (XBE_XINPUT_PACKET).
#include <cstdint>
#include <cstring>

namespace det {

template <typename T>
inline bool XInputReportChanged(const T& a, const T& b) {
    if (a.connected != b.connected || a.buttons != b.buttons) return true;
    if (std::memcmp(a.analog, b.analog, 8) != 0) return true;
    if (a.lx != b.lx || a.ly != b.ly || a.rx != b.rx || a.ry != b.ry) return true;
    return false;
}

}  // namespace det
