#!/usr/bin/env python3
"""Insert default-off Tracy zones into the sdk working tree.

Safe to run twice. The Apple patch and the lockstep patch are applied first;
this script follows whichever of those the tree already has. A missing
DirectSound clocked mixer or a missing OnPresented is the upstream tree, and
the zones land on the functions that tree does have.

    python3 tools/apply-tracy.py [sdk-dir]
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SDK = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "sdk"
HEADER = (ROOT / "tools" / "tracy" / "trace.h").read_text()


def die(msg):
    print(f"apply-tracy: {msg}", file=sys.stderr)
    raise SystemExit(1)


def read(rel):
    path = SDK / rel
    if not path.is_file():
        return None
    return path.read_text()


def write(rel, text):
    (SDK / rel).write_text(text)


def ensure_include(text, rel):
    line = '#include "core/trace.h"\n'
    if line in text:
        return text
    idx = text.find('#include "')
    if idx < 0:
        die(f"{rel} has no quoted include")
    end = text.find("\n", idx) + 1
    return text[:end] + line + text[end:]


def insert_after(text, signature, block, label):
    found = text.count(signature)
    if found != 1:
        die(f"{label}: expected 1 occurrence of the signature, found {found}")
    at = text.find(signature) + len(signature)
    nxt = text.find("\n", at)
    if "XBE_TRACY_ZONES" in text[at:nxt]:
        return text
    return text[:at] + block + text[at:]


def zone_after(rel, signature, zone_name):
    text = read(rel)
    if text is None:
        die(f"missing {rel}")
    block = f'    XBE_ZONE("{zone_name}"); // XBE_TRACY_ZONES\n'
    text = ensure_include(text, rel)
    text = insert_after(text, signature, block, f"{rel} {zone_name}")
    write(rel, text)


def present_frame(has_presented):
    rel = "runtime/xdk/d3d8.cpp"
    text = read(rel)
    if text is None:
        die(f"missing {rel}")
    text = ensure_include(text, rel)
    text = insert_after(
        text,
        "uint32_t PresentFrame(const char* name) {\n",
        "    xbe_trace::EndGameTick(); // XBE_TRACY_ZONES\n"
        "    xbe_trace::BeginFrame();\n",
        "PresentFrame",
    )
    end = "    SetDev(kDevSwapCount, n);\n    return n;\n"
    done = "    xbe_trace::EndFrame(); // XBE_TRACY_ZONES\n"
    if done not in text:
        if text.count(end) != 1:
            die(f"PresentFrame return anchor found {text.count(end)} times")
        tail = "    xbe_trace::EndFrame(); // XBE_TRACY_ZONES\n    xbe_trace::MarkFrame();\n"
        if not has_presented:
            tail += "    xbe_trace::BeginGameTick();\n"
        text = text.replace(end, "    SetDev(kDevSwapCount, n);\n" + tail + "    return n;\n", 1)
    write(rel, text)


def game_tick():
    rel = "runtime/core/determinism.cpp"
    text = read(rel)
    if text is None or "void OnPresented(" not in text:
        return False
    text = ensure_include(text, rel)
    anchor = "    g_in_present = false;\n"
    # The declaration is `bool g_in_present = false;`. This is the assignment.
    if "xbe_trace::BeginGameTick(); // XBE_TRACY_ZONES\n" + anchor not in text:
        if text.count(anchor) != 1:
            die(f"OnPresented tick anchor found {text.count(anchor)} times")
        text = text.replace(
            anchor,
            "    xbe_trace::BeginGameTick(); // XBE_TRACY_ZONES\n" + anchor,
            1,
        )
    write(rel, text)
    return True


def audio():
    rel = "runtime/xdk/dsound.cpp"
    text = read(rel)
    if text is None:
        die(f"missing {rel}")
    text = ensure_include(text, rel)
    clocked = "void RenderClocked(float* out, uint32_t frames, bool clock) {\n"
    plain = "void Render(float* out, uint32_t frames) {\n"
    block = '    XBE_ZONE("audio"); // XBE_TRACY_ZONES\n'
    if clocked in text:
        text = insert_after(text, clocked, block, "RenderClocked")
    elif plain in text:
        text = insert_after(text, plain, block, "Render")
    else:
        die("no Render or RenderClocked")
    write(rel, text)


def main():
    if not (SDK / "runtime" / "gpu" / "vulkan.cpp").is_file():
        die("sdk/ is empty. Run: git submodule update --init sdk")
    dest = SDK / "runtime" / "core" / "trace.h"
    dest.parent.mkdir(parents=True, exist_ok=True)
    if not dest.is_file() or dest.read_text() != HEADER:
        dest.write_text(HEADER)
    has_presented = game_tick()
    present_frame(has_presented)
    zone_after(
        "runtime/gpu/vulkan.cpp",
        "void VulkanBackend::Submit(bool wait, VkSemaphore waitSem, VkSemaphore signalSem) {\n",
        "gpu submit",
    )
    zone_after(
        "runtime/gpu/vulkan.cpp",
        "void VulkanBackend::Present(const Surface& front) {\n",
        "gpu present",
    )
    zone_after(
        "runtime/gpu/d3d12.cpp",
        "void D3D12Backend::Submit(bool wait) {\n",
        "gpu submit",
    )
    zone_after(
        "runtime/gpu/d3d12.cpp",
        "void D3D12Backend::Present(const Surface& front) {\n",
        "gpu present",
    )
    audio()
    print(f"apply-tracy: zones in {SDK}" + (" (tick closes in OnPresented)" if has_presented else " (tick closes at PresentFrame)"))


if __name__ == "__main__":
    main()
