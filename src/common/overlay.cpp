// the in-game overlay: F2 a performance panel, F1 a settings panel, both off until asked for.
// app::GameInfo::ui calls Draw on the render thread, inside the renderer's own ImGui frame and
// under app::UiLock, so nothing here may block, allocate per frame or trust that a context, a
// window size or a pad exists.
#include "common/overlay.h"

#include "common/cheats.h"
#include "common/input_kbm.h"
#include "net/relay_client.h"
#include "core/log.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iterator>
#include <chrono>
#include <thread>
#include <utility>

#include <imgui.h>

#include "app/app.h"
#if !defined(_WIN32)
#include "app/platform.h"  // app::HostAspect
#endif
#include "gpu/backend.h"

namespace overlay {

namespace {

// the guest frame is taken as the console's 640x480, as game_info.cpp's picture rect takes it too:
// what this title asks CreateDevice for has not been checked yet (the runtime logs it as
// "d3d: CreateDevice"). The host frame target is that times the render scale, widened for
// ultrawide only on a backend that actually widens (see BackendWiden, D9); the inner rounding
// happens in guest pixels, before the scale
constexpr float kGuestW = 640.0f, kGuestH = 480.0f;

constexpr int kSamples = 120;  // frame times behind the graph, two seconds at 60 fps

// the frame time ring: written every frame whether or not the panel is up, so the graph is
// already full the moment F2 opens it
struct Ring {
    float ms[kSamples] = {};
    int next = 0;   // where the next sample goes, and the oldest one once the ring is full
    int count = 0;  // samples in the ring, up to kSamples
};
Ring g_ring;

bool g_perf_open = false, g_settings_open = false;
bool g_settings_focus = false;  // the settings window takes focus the frame it opens
float g_ui_scale = 0.0f;        // style and font scale in use, 0 before the first frame
// g_settings_open is the render thread's; this is the copy the window and guest threads read
std::atomic<bool> g_settings_up{false};

// the settings panel edits a copy and only writes it back on Apply, so a half typed resolution
// never reaches the renderer
struct Draft {
    app::DisplayMode mode = app::DisplayMode::Borderless;
    uint32_t width = 0, height = 0;
    char aspect[12] = "auto";
    bool ultrawide = true;
    bool vsync = true;
    uint32_t fps_cap = 60;
    int render_percent = 100;
    uint32_t aniso = 0;
    int hdr = 0;            // index into kHdrKeys, app::HdrMode's order
    int paper_white = 200;  // nits
    int peak = 0;           // nits, 0 the display's own maximum
};
Draft g_draft;
app::Mode g_desktop{0, 0};   // read when the panel opens: the mode lookup is a syscall
char g_status[80] = "";      // what the last Apply or Save did
float g_status_left = 0.0f;  // seconds it stays up

constexpr app::Mode kResolutions[] = {
    {1280, 720},  {1280, 800},  {1600, 900},  {1680, 1050}, {1920, 1080}, {1920, 1200},
    {2560, 1080}, {2560, 1440}, {2560, 1600}, {3440, 1440}, {3840, 1080}, {3840, 1600},
    {3840, 2160}, {5120, 1440},
};
constexpr uint32_t kFpsCaps[] = {0, 30, 60, 72, 120, 144, 165, 240};
constexpr uint32_t kAniso[] = {0, 2, 4, 8, 16};
const char* const kAnisoNames[] = {"As the game asks", "2x", "4x", "8x", "16x"};
const char* const kAspects[] = {"auto", "4:3", "16:9", "16:10", "21:9", "32:9"};
const char* const kModeNames[] = {"Fullscreen", "Borderless", "Windowed"};
// app::HdrMode's order: the setting's string, and what the panel calls it
const char* const kHdrKeys[] = {"off", "auto", "on"};
const char* const kHdrNames[] = {"Off", "Auto", "On"};
// the renderer takes 0 (the display's own figure) or 80 to 10000 nits; the slider offers the part
// of that a player can judge by eye, from below this machine's dimmest HDR panel (417 nits) up
constexpr int kPeakMin = 100, kPeakMax = 2000, kNitStep = 50;

// the dark style this overlay wears, built once at scale 1 and rescaled from it
const ImGuiStyle& BaseStyle() {
    static const ImGuiStyle base = [] {
        ImGuiStyle s;
        ImGui::StyleColorsDark(&s);
        const ImVec4 accent{0.29f, 0.56f, 0.78f, 1.00f};  // muted steel blue, not the ImGui blue
        const ImVec4 accent_hot{0.40f, 0.68f, 0.92f, 1.00f};
        ImVec4* c = s.Colors;
        c[ImGuiCol_Text] = {0.90f, 0.92f, 0.94f, 1.00f};
        c[ImGuiCol_TextDisabled] = {0.48f, 0.52f, 0.58f, 1.00f};
        c[ImGuiCol_WindowBg] = {0.06f, 0.07f, 0.09f, 0.96f};
        c[ImGuiCol_ChildBg] = {0.09f, 0.10f, 0.13f, 0.60f};
        c[ImGuiCol_PopupBg] = {0.08f, 0.09f, 0.11f, 0.98f};
        c[ImGuiCol_Border] = {1.00f, 1.00f, 1.00f, 0.08f};
        c[ImGuiCol_BorderShadow] = {0.00f, 0.00f, 0.00f, 0.00f};
        c[ImGuiCol_FrameBg] = {0.15f, 0.17f, 0.21f, 1.00f};
        c[ImGuiCol_FrameBgHovered] = {0.20f, 0.23f, 0.28f, 1.00f};
        c[ImGuiCol_FrameBgActive] = {0.24f, 0.28f, 0.34f, 1.00f};
        c[ImGuiCol_TitleBg] = {0.06f, 0.07f, 0.09f, 1.00f};
        c[ImGuiCol_TitleBgActive] = {0.10f, 0.12f, 0.15f, 1.00f};
        c[ImGuiCol_TitleBgCollapsed] = {0.06f, 0.07f, 0.09f, 0.80f};
        c[ImGuiCol_ScrollbarBg] = {0.00f, 0.00f, 0.00f, 0.00f};
        c[ImGuiCol_ScrollbarGrab] = {0.24f, 0.28f, 0.34f, 1.00f};
        c[ImGuiCol_ScrollbarGrabHovered] = {0.31f, 0.36f, 0.43f, 1.00f};
        c[ImGuiCol_ScrollbarGrabActive] = accent;
        c[ImGuiCol_CheckMark] = accent_hot;
        c[ImGuiCol_SliderGrab] = accent;
        c[ImGuiCol_SliderGrabActive] = accent_hot;
        c[ImGuiCol_Button] = {0.17f, 0.20f, 0.25f, 1.00f};
        c[ImGuiCol_ButtonHovered] = {0.24f, 0.33f, 0.43f, 1.00f};
        c[ImGuiCol_ButtonActive] = accent;
        c[ImGuiCol_Header] = {0.29f, 0.56f, 0.78f, 0.32f};
        c[ImGuiCol_HeaderHovered] = {0.29f, 0.56f, 0.78f, 0.55f};
        c[ImGuiCol_HeaderActive] = accent;
        c[ImGuiCol_Separator] = {1.00f, 1.00f, 1.00f, 0.10f};
        c[ImGuiCol_SeparatorHovered] = {0.29f, 0.56f, 0.78f, 0.55f};
        c[ImGuiCol_SeparatorActive] = accent;
        c[ImGuiCol_ResizeGrip] = {1.00f, 1.00f, 1.00f, 0.06f};
        c[ImGuiCol_ResizeGripHovered] = {0.29f, 0.56f, 0.78f, 0.55f};
        c[ImGuiCol_ResizeGripActive] = accent;
        c[ImGuiCol_PlotLines] = accent_hot;
        c[ImGuiCol_PlotLinesHovered] = accent_hot;
        c[ImGuiCol_TextSelectedBg] = {0.29f, 0.56f, 0.78f, 0.35f};
        c[ImGuiCol_NavHighlight] = accent;
        s.WindowPadding = {18.0f, 16.0f};
        s.FramePadding = {10.0f, 6.0f};
        s.ItemSpacing = {10.0f, 8.0f};
        s.ItemInnerSpacing = {8.0f, 6.0f};
        s.CellPadding = {8.0f, 5.0f};
        s.IndentSpacing = 18.0f;
        s.ScrollbarSize = 12.0f;
        s.GrabMinSize = 12.0f;
        s.WindowBorderSize = 1.0f;
        s.ChildBorderSize = 1.0f;
        s.PopupBorderSize = 1.0f;
        s.FrameBorderSize = 0.0f;
        s.WindowRounding = 9.0f;
        s.ChildRounding = 7.0f;
        s.FrameRounding = 5.0f;
        s.PopupRounding = 7.0f;
        s.ScrollbarRounding = 9.0f;
        s.GrabRounding = 5.0f;
        s.TabRounding = 6.0f;
        s.WindowTitleAlign = {0.5f, 0.5f};
        s.SeparatorTextBorderSize = 1.0f;
        s.SeparatorTextPadding = {0.0f, 10.0f};
        s.SeparatorTextAlign = {0.0f, 0.5f};
        s.AntiAliasedLines = true;
        s.AntiAliasedFill = true;
        return s;
    }();
    return base;
}

// readable on a 3840x2160 window instead of microscopic: everything grows with the output height,
// in quarter steps so the style is only rebuilt when the window really changes size
void ScaleStyle(ImGuiIO& io) {
    float want = std::clamp(std::round(io.DisplaySize.y / 1080.0f * 4.0f) / 4.0f, 1.0f, 4.0f);
    if (std::fabs(want - g_ui_scale) < 1e-3f) return;
    g_ui_scale = want;
    ImGui::GetStyle() = BaseStyle();
    ImGui::GetStyle().ScaleAllSizes(want);
    io.FontGlobalScale = want;  // ImGui picks the font size up at the next NewFrame
}

void Sample(float dt) {
    // a stall between presents (a load, an alt-tab) would otherwise flatten the graph for seconds
    g_ring.ms[g_ring.next] = std::clamp(dt * 1000.0f, 0.0f, 200.0f);
    g_ring.next = (g_ring.next + 1) % kSamples;
    if (g_ring.count < kSamples) g_ring.count++;
}

// the name of an aspect when it is one of the usual ones, else null
const char* AspectName(float a) {
    struct Named {
        float ratio;
        const char* name;
    };
    static constexpr Named kNamed[] = {
        {4.0f / 3.0f, "4:3"},    {16.0f / 10.0f, "16:10"}, {16.0f / 9.0f, "16:9"},
        {64.0f / 27.0f, "21:9"}, {32.0f / 9.0f, "32:9"},
    };
    for (const Named& n : kNamed)
        if (std::fabs(a - n.ratio) < 0.02f) return n.name;
    return nullptr;
}

// Hor+ widening is a backend property, not a settings property. The Windows
// D3D12 backend widens its frame target by app::AspectWiden(); the Vulkan
// backend widens it to app::HostAspect() at the same height (0 = unwidened).
// The panel reports the widening the active backend actually builds.
float BackendWiden() {
#if defined(_WIN32)
    return app::AspectWiden();
#else
    float aspect = app::HostAspect();
    return aspect > 0.0f ? aspect * (kGuestH / kGuestW) : 1.0f;
#endif
}

// the host frame target for a render scale, exactly as the active backend builds it
void FrameTarget(float scale, float widen, uint32_t& w, uint32_t& h) {
    long guest_w = std::lround(kGuestW * widen);
    w = uint32_t(std::max<long>(std::lround(guest_w * scale), 1));
    h = uint32_t(std::max<long>(std::lround(kGuestH * scale), 1));
}

void DrawPerf(double fps) {
    const ImGuiIO& io = ImGui::GetIO();
    const ImGuiStyle& st = ImGui::GetStyle();
    // pinned to the top right corner, out of the way of anything the game puts on screen
    ImGui::SetNextWindowPos({io.DisplaySize.x - st.WindowPadding.x, st.WindowPadding.y}, ImGuiCond_Always,
                            {1.0f, 0.0f});
    ImGui::SetNextWindowSize({270.0f * g_ui_scale, 0.0f}, ImGuiCond_Always);  // height auto fits
    ImGui::SetNextWindowBgAlpha(0.45f);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMouseInputs;
    if (!ImGui::Begin("##fuzion_perf", nullptr, kFlags)) {
        ImGui::End();
        return;
    }

    float last = 0.0f, lo = 0.0f, hi = 0.0f, sum = 0.0f;
    if (g_ring.count > 0) {
        // the ring fills from 0, so entries below count are always the real samples
        lo = hi = g_ring.ms[0];
        for (int i = 0; i < g_ring.count; i++) {
            float v = g_ring.ms[i];
            lo = std::min(lo, v);
            hi = std::max(hi, v);
            sum += v;
        }
        last = g_ring.ms[(g_ring.next + kSamples - 1) % kSamples];
    }

    ImGui::PushStyleColor(ImGuiCol_Text, st.Colors[ImGuiCol_PlotLines]);
    ImGui::Text("%.1f fps", fps);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, st.ItemSpacing.x * 2.0f);
    ImGui::Text("%.2f ms", last);

    char graph[48];
    std::snprintf(graph, sizeof graph, "%.1f - %.1f ms, avg %.1f", lo, hi,
                  g_ring.count ? sum / float(g_ring.count) : 0.0f);
    // values_offset is the oldest sample: the write cursor once the ring has wrapped
    ImGui::PlotLines("##frametime", g_ring.ms, g_ring.count, g_ring.count == kSamples ? g_ring.next : 0, graph,
                     0.0f, hi * 1.25f + 1.0f, {ImGui::GetContentRegionAvail().x, 44.0f * g_ui_scale});

    float widen = BackendWiden();
    float scale = app::RenderScale(uint32_t(kGuestH));
    uint32_t rw = 0, rh = 0;
    FrameTarget(scale, widen, rw, rh);  // D9: the target the backend actually builds
#if defined(_WIN32)
    float aspect = app::Widescreen() ? 16.0f / 9.0f * app::AspectWiden() : 4.0f / 3.0f;
#else
    float host_aspect = app::HostAspect();
    float aspect = host_aspect > 0.0f ? host_aspect : (app::Widescreen() ? 16.0f / 9.0f : 4.0f / 3.0f);
#endif

    const float col = ImGui::CalcTextSize("render ").x;
    char value[48];
    auto row = [col, &value](const char* label) {
        ImGui::TextDisabled("%s", label);
        ImGui::SameLine(col);
        ImGui::TextUnformatted(value);
    };
    std::snprintf(value, sizeof value, "%.0f x %.0f", io.DisplaySize.x, io.DisplaySize.y);
    row("host");
    std::snprintf(value, sizeof value, "%u x %u  (%u%%)", rw, rh, app::Get().render_percent);
    row("render");
    if (const char* name = AspectName(aspect)) std::snprintf(value, sizeof value, "%s  (%.2f)", name, aspect);
    else std::snprintf(value, sizeof value, "%.2f", aspect);
    row("aspect");

    // backend counters (gpu/backend.h): cumulative title-side time, shown in milliseconds
    std::snprintf(value, sizeof value, "%.1f ms", double(gpu::g_perf_capture_ns.load(std::memory_order_relaxed)) / 1e6);
    row("capture");
    std::snprintf(value, sizeof value, "%.1f ms", double(gpu::g_perf_hle_ns.load(std::memory_order_relaxed)) / 1e6);
    row("hle");
    std::snprintf(value, sizeof value, "%.1f ms / %llu", double(gpu::g_perf_wait_ns.load(std::memory_order_relaxed)) / 1e6,
                  (unsigned long long)gpu::g_perf_waits.load(std::memory_order_relaxed));
    row("wait");
    std::snprintf(value, sizeof value, "%llu late / %llu block",
                  (unsigned long long)gpu::g_perf_sync_late.load(std::memory_order_relaxed),
                  (unsigned long long)gpu::g_perf_sync_block.load(std::memory_order_relaxed));
    row("sync");

    ImGui::Spacing();
    ImGui::TextDisabled("F1 settings");
    ImGui::End();
}

void SyncDraft() {
    const app::Settings& s = app::Get();
    g_draft.mode = s.display_mode;
    g_draft.width = s.width;
    g_draft.height = s.height;
    std::snprintf(g_draft.aspect, sizeof g_draft.aspect, "%s", s.aspect_ratio.c_str());
    g_draft.ultrawide = s.ultrawide;
    g_draft.vsync = s.vsync;
    g_draft.fps_cap = s.fps_cap;
    g_draft.render_percent = int(s.render_percent);
    g_draft.aniso = s.aniso;
    g_draft.hdr = int(app::HdrWanted());
    g_draft.paper_white = int(app::PaperWhiteNits());
    g_draft.peak = int(app::PeakNits());
    g_desktop = app::Desktop();
    g_status[0] = '\0';
    g_status_left = 0.0f;
}

void Status(const char* text) {
    std::snprintf(g_status, sizeof g_status, "%s", text);
    g_status_left = 4.0f;
}

void Apply() {
    app::Settings& s = app::Get();
    // the renderer re-reads the settings every present and remakes the swap chain itself when the
    // HDR mode or the display changes, so HDR must NOT come through here: an ApplyDisplay
    // recentres a windowed window and, fullscreen at a non-desktop resolution, resets and re-sets
    // the display mode. Only the window's own mode or size does.
    bool relayout = s.display_mode != g_draft.mode || s.width != g_draft.width || s.height != g_draft.height;
    s.display_mode = g_draft.mode;
    s.width = g_draft.width;
    s.height = g_draft.height;
    // the guest threads read this setting too (app::Widescreen and friends): every choice is
    // short enough to live in the string's own storage, so writing it never moves a buffer
    s.aspect_ratio = g_draft.aspect;
    s.ultrawide = g_draft.ultrawide;
    s.vsync = g_draft.vsync;
    s.fps_cap = g_draft.fps_cap;
    s.render_percent = uint32_t(std::max(g_draft.render_percent, 1));
    s.aniso = g_draft.aniso;
    // like the aspect, each choice fits the string's own storage
    s.hdr = kHdrKeys[g_draft.hdr];
    s.hdr_paper_white = uint32_t(g_draft.paper_white);
    s.hdr_peak = uint32_t(g_draft.peak);
    if (relayout) app::ApplyDisplay();
}

// the label column of a settings row, with the control stretched across the rest
void Row(const char* label) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

// what a control really does, under it, wrapped to the column
void Note(const char* text) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", text);
    ImGui::PopTextWrapPos();
}

bool BeginRows(const char* id, float label_width) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit)) return false;
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, label_width);
    ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

void DrawDisplay() {
    Row("Display mode");
    int mode = int(g_draft.mode);
    if (ImGui::Combo("##mode", &mode, kModeNames, int(std::size(kModeNames)))) g_draft.mode = app::DisplayMode(mode);

    // Borderless always fills the display it is on (main_sdl.cpp's ApplyNow, main.cpp's
    // ApplyNow alike), so a chosen resolution can never size its window: the control is
    // greyed there rather than offered and silently ignored. It stays editable in the
    // two modes that honour it, and the stored value survives a mode switch.
    const bool borderless = g_draft.mode == app::DisplayMode::Borderless;
    Row("Resolution");
    ImGui::BeginDisabled(borderless);
    char text[48];
    if (g_draft.width && g_draft.height) std::snprintf(text, sizeof text, "%u x %u", g_draft.width, g_draft.height);
    else std::snprintf(text, sizeof text, "Desktop (%u x %u)", g_desktop.width, g_desktop.height);
    if (ImGui::BeginCombo("##resolution", text)) {
        std::snprintf(text, sizeof text, "Desktop (%u x %u)", g_desktop.width, g_desktop.height);
        if (ImGui::Selectable(text, !g_draft.width || !g_draft.height)) g_draft.width = g_draft.height = 0;
        for (const app::Mode& m : kResolutions) {
            std::snprintf(text, sizeof text, "%u x %u", m.width, m.height);
            if (ImGui::Selectable(text, g_draft.width == m.width && g_draft.height == m.height)) {
                g_draft.width = m.width;
                g_draft.height = m.height;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    Note(borderless ? "Borderless follows the display, so no resolution applies here. Choose "
                      "Windowed or Fullscreen to set one."
                    : "Windowed sizes the window to this. Fullscreen presents at this size: a real "
                      "display-mode switch where the platform allows one, scaled to the display "
                      "where it does not (Wayland). Desktop is the display's own size.");

    Row("Aspect ratio");
    if (ImGui::BeginCombo("##aspect", g_draft.aspect)) {
        for (const char* a : kAspects)
            if (ImGui::Selectable(a, std::strcmp(a, g_draft.aspect) == 0))
                std::snprintf(g_draft.aspect, sizeof g_draft.aspect, "%s", a);
        ImGui::EndCombo();
    }
    Note("Auto follows the shape of the output itself.");

    Row("Ultrawide");
    ImGui::Checkbox("##ultrawide", &g_draft.ultrawide);
    Note("Widens the view on a display wider than 16:9, and does nothing on one that is not.");

    Row("Vertical sync");
    ImGui::Checkbox("##vsync", &g_draft.vsync);

    Row("Frame rate cap");
    if (g_draft.fps_cap) std::snprintf(text, sizeof text, "%u fps", g_draft.fps_cap);
    else std::snprintf(text, sizeof text, "Unlimited");
    if (ImGui::BeginCombo("##fpscap", text)) {
        for (uint32_t cap : kFpsCaps) {
            if (cap) std::snprintf(text, sizeof text, "%u fps", cap);
            else std::snprintf(text, sizeof text, "Unlimited");
            if (ImGui::Selectable(text, g_draft.fps_cap == cap)) g_draft.fps_cap = cap;
        }
        ImGui::EndCombo();
    }
    Note("Caps the presented frame rate; the game still steps by the time that really passed.");

    Row("HDR");
    ImGui::Combo("##hdr", &g_draft.hdr, kHdrNames, int(std::size(kHdrNames)));
    // what the swap chain made of the setting, not the draft: it changes on Apply, or when the
    // window moves to another display
    const app::HdrStatus hdr = app::Hdr();
    char status[160];
    // `why` is set whenever it has something to say, including while HDR is active: "on" forced
    // onto a display running in SDR is active but not really HDR, and the player needs to be told
    // that rather than left reading a peak number that means nothing
    if (hdr.active && hdr.why && *hdr.why)
        std::snprintf(status, sizeof status, "HDR is on: %s.", hdr.why);
    else if (hdr.active && hdr.display_max_nits)
        std::snprintf(status, sizeof status, "HDR is on: this display peaks at %u nits.", hdr.display_max_nits);
    else if (hdr.active)
        std::snprintf(status, sizeof status, "HDR is on: this display does not report its peak.");
    else if (hdr.why && *hdr.why)
        std::snprintf(status, sizeof status, "HDR is off: %s.", hdr.why);
    else
        std::snprintf(status, sizeof status, "HDR is off.");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(status);
    ImGui::PopTextWrapPos();
    Note("Auto uses HDR when the display the window is on is running in HDR; On asks for it "
         "whatever the display reports; Off keeps the picture exactly as it always was.");

    // the two brightness numbers only mean anything to an HDR swap chain
    ImGui::BeginDisabled(!hdr.active);
    Row("Paper white");
    if (ImGui::SliderInt("##paperwhite", &g_draft.paper_white, 80, 480, "%d nits", ImGuiSliderFlags_AlwaysClamp))
        g_draft.paper_white = (g_draft.paper_white + 5) / 10 * 10;
    Note("How bright the game's white is drawn. Raise it if the picture looks dim next to the desktop.");

    Row("Peak brightness");
    if (g_draft.peak) std::snprintf(text, sizeof text, "%%d nits");
    else if (hdr.display_max_nits) std::snprintf(text, sizeof text, "Display maximum (%u nits)", hdr.display_max_nits);
    else std::snprintf(text, sizeof text, "Display maximum");
    if (ImGui::SliderInt("##peak", &g_draft.peak, 0, kPeakMax, text, ImGuiSliderFlags_AlwaysClamp)) {
        // the far left is the display's own figure; past it, whole steps from kPeakMin up
        if (g_draft.peak < kPeakMin) g_draft.peak = g_draft.peak < kPeakMin / 2 ? 0 : kPeakMin;
        else g_draft.peak = (g_draft.peak + kNitStep / 2) / kNitStep * kNitStep;
    }
    Note("The brightest the picture may get. Leave it at the display's maximum unless highlights "
         "look clipped or harsh on this screen.");
    ImGui::EndDisabled();
}

void DrawQuality() {
    Row("Render scale");
    ImGui::SliderInt("##renderscale", &g_draft.render_percent, 50, 200, "%d %%");
    // the same arithmetic the renderer does: the output height, this share of it, over the guest's
    app::Mode out = g_draft.width && g_draft.height ? app::Mode{g_draft.width, g_draft.height} : g_desktop;
    float scale = std::max(out.height * (g_draft.render_percent / 100.0f) / kGuestH, 0.1f);
    uint32_t rw = 0, rh = 0;
    FrameTarget(scale, BackendWiden(), rw, rh);  // D9
    char text[80];
    std::snprintf(text, sizeof text, "%u x %u frame target, from the guest's 640 x 480.", rw, rh);
    Note(text);

    Row("Anisotropic filtering");
    int aniso = 0;
    for (int i = 0; i < int(std::size(kAniso)); i++)
        if (kAniso[i] == g_draft.aniso) aniso = i;
    if (ImGui::Combo("##aniso", &aniso, kAnisoNames, int(std::size(kAnisoNames)))) g_draft.aniso = kAniso[aniso];
    Note("Forced only where the game already filters across mip levels.");
}

// the fixed keyboard-and-mouse bindings (input_kbm.cpp): shown, not edited - the layout is the
// port's control scheme, not a per-player setting
void DrawInput() {
    Row("Move / menus");
    Note("W A S D moves (left stick); arrow keys are the D-pad.");
    Row("Camera");
    Note("Hold the right mouse button and drag, or Q / E to rotate and R / F or the mouse wheel to zoom.");
    Row("Buttons");
    Note("Space A, Escape/Backspace B, X X, Y Y, Z / C triggers, 1 / 2 black / white, Enter Start, "
         "Tab Back, Shift / Ctrl (or middle button) stick clicks.");
}

// The game's own console cheats, as many as have been identified (common/cheats.h). Each one is
// entered the way its pad code is, so a checkbox is the game's own state of that code and what
// follows is the game's.
void DrawCheats(bool ingame) {
    // a click only queues the code: the title thread enters it at the top of its next frame
    ImGui::BeginDisabled(!ingame);
    for (const cheats::Cheat& r : cheats::Table()) {
        Row(r.label);
        bool on = cheats::On(r.code);
        ImGui::PushID(int(r.code));
        if (ImGui::Checkbox("##cheat", &on)) cheats::Toggle(r.code);
        ImGui::PopID();
        Note(r.note);
    }
    ImGui::EndDisabled();
}


// F1 internet lobby. Draw only reads these. A click starts one worker.
constexpr int kRelayAddr = 64;
constexpr int kRelayLink = 128;
constexpr int kRelayStatus = 160;
constexpr int kGameCap = 32;
char g_relay_addr[kRelayAddr] = "127.0.0.1:9999";
char g_link_edit[kRelayLink] = "";
char g_name_edit[net::RelayClient::kNameLen + 1] = "Game";
bool g_link_only = false;
char g_relay_seen[kRelayLink] = "";
char g_relay_link[kRelayLink] = "";
char g_relay_code[net::RelayClient::kCodeLen + 1] = "";
char g_relay_status[kRelayStatus] = "";
std::atomic<uint32_t> g_relay_seq{0};
std::atomic<int> g_relay_busy{0};
std::atomic<int> g_relay_seat{0};
std::atomic<int> g_beat{0};
std::atomic<int> g_fault_panel{0};
net::RelayClient::Game g_games[kGameCap];
int g_game_n = 0;
std::atomic<uint32_t> g_list_seq{0};
std::atomic<int> g_list_known{0};

void PublishRelay(const char* status, const char* link, const char* code = nullptr) {
    uint32_t s = g_relay_seq.load(std::memory_order_relaxed);
    g_relay_seq.store(s + 1, std::memory_order_relaxed);
    if (status) std::snprintf(g_relay_status, sizeof g_relay_status, "%s", status);
    if (link) std::snprintf(g_relay_link, sizeof g_relay_link, "%s", link);
    if (code) std::snprintf(g_relay_code, sizeof g_relay_code, "%s", code);
    g_relay_seq.store(s + 2, std::memory_order_release);
}

void ReadRelay(char* status, int status_n, char* link, int link_n, char* code, int code_n) {
    for (;;) {
        uint32_t a = g_relay_seq.load(std::memory_order_acquire);
        if (a & 1u) continue;
        char st[kRelayStatus];
        char lk[kRelayLink];
        char cd[sizeof g_relay_code];
        std::memcpy(st, g_relay_status, sizeof st);
        std::memcpy(lk, g_relay_link, sizeof lk);
        std::memcpy(cd, g_relay_code, sizeof cd);
        st[sizeof st - 1] = '\0';
        lk[sizeof lk - 1] = '\0';
        cd[sizeof cd - 1] = '\0';
        uint32_t b = g_relay_seq.load(std::memory_order_acquire);
        if (a != b) continue;
        std::snprintf(status, size_t(status_n), "%s", st);
        std::snprintf(link, size_t(link_n), "%s", lk);
        if (code && code_n > 0) std::snprintf(code, size_t(code_n), "%s", cd);
        return;
    }
}

void PublishGames(const net::RelayClient::Game* rows, int n) {
    if (n < 0) n = 0;
    if (n > kGameCap) n = kGameCap;
    uint32_t s = g_list_seq.load(std::memory_order_relaxed);
    g_list_seq.store(s + 1, std::memory_order_relaxed);
    if (n > 0 && rows) std::memcpy(g_games, rows, size_t(n) * sizeof(net::RelayClient::Game));
    g_game_n = n;
    g_list_seq.store(s + 2, std::memory_order_release);
    g_list_known.store(1, std::memory_order_release);
}

int ReadGames(net::RelayClient::Game* out, int cap) {
    if (!out || cap <= 0) return 0;
    for (;;) {
        uint32_t a = g_list_seq.load(std::memory_order_acquire);
        if (a & 1u) continue;
        int n = g_game_n;
        if (n > cap) n = cap;
        if (n > 0) std::memcpy(out, g_games, size_t(n) * sizeof(net::RelayClient::Game));
        uint32_t b = g_list_seq.load(std::memory_order_acquire);
        if (a != b) continue;
        return n;
    }
}

void ShowFault(const char* status) {
    PublishRelay(status ? status : "The match stopped.", nullptr, nullptr);
    g_fault_panel.store(1, std::memory_order_release);
}

int HexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool ParseAddr(const char* text, char* host, int host_n, uint16_t* port) {
    if (!text || !host || !port || host_n < 2) return false;
    const char* colon = std::strrchr(text, ':');
    if (!colon || colon == text) return false;
    int n = int(colon - text);
    if (n <= 0 || n >= host_n) return false;
    std::memcpy(host, text, size_t(n));
    host[n] = '\0';
    int p = std::atoi(colon + 1);
    if (p <= 0 || p > 65535) return false;
    *port = uint16_t(p);
    return true;
}

bool ParseLink(const char* text, char* host, int host_n, uint16_t* port, uint8_t token[net::RelayClient::kTokenLen]) {
    if (!text || std::strncmp(text, "ff1:", 4) != 0) return false;
    const char* hex = std::strrchr(text, ':');
    if (!hex || hex[1] == '\0') return false;
    const char* port_colon = nullptr;
    for (const char* p = text + 4; p < hex; p++)
        if (*p == ':') port_colon = p;
    if (!port_colon) return false;
    int n = int(port_colon - (text + 4));
    if (n <= 0 || n >= host_n) return false;
    std::memcpy(host, text + 4, size_t(n));
    host[n] = '\0';
    char port_buf[8];
    int pn = int(hex - (port_colon + 1));
    if (pn <= 0 || pn >= int(sizeof port_buf)) return false;
    std::memcpy(port_buf, port_colon + 1, size_t(pn));
    port_buf[pn] = '\0';
    int p = std::atoi(port_buf);
    if (p <= 0 || p > 65535) return false;
    *port = uint16_t(p);
    if (std::strlen(hex + 1) != 32) return false;
    for (int i = 0; i < net::RelayClient::kTokenLen; i++) {
        int hi = HexNibble(hex[1 + i * 2]);
        int lo = HexNibble(hex[2 + i * 2]);
        if (hi < 0 || lo < 0) return false;
        token[i] = uint8_t((hi << 4) | lo);
    }
    return true;
}

void FormatLink(char* out, int out_n, const char* host, uint16_t port, const uint8_t token[net::RelayClient::kTokenLen]) {
    char hex[33];
    for (int i = 0; i < net::RelayClient::kTokenLen; i++) std::snprintf(hex + i * 2, 3, "%02x", token[i]);
    std::snprintf(out, size_t(out_n), "ff1:%s:%u:%s", host, unsigned(port), hex);
}

bool CodeChar(char c) {
    for (const char* p = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; *p; p++)
        if (*p == c) return true;
    return false;
}

bool ParseCode(const char* text, char code[net::RelayClient::kCodeLen + 1]) {
    if (!text || !code) return false;
    if (std::strncmp(text, "ff1:", 4) == 0) text += 4;
    if (std::strchr(text, ':')) return false;
    if (std::strlen(text) != net::RelayClient::kCodeLen) return false;
    for (int i = 0; i < net::RelayClient::kCodeLen; i++) {
        char c = text[i];
        if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
        if (!CodeChar(c)) return false;
        code[i] = c;
    }
    code[net::RelayClient::kCodeLen] = '\0';
    return true;
}

void StartBeat() {
    int gen = g_beat.fetch_add(1, std::memory_order_acq_rel) + 1;
    std::thread([gen] {
        while (g_beat.load(std::memory_order_acquire) == gen) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (g_beat.load(std::memory_order_acquire) != gen) return;
            if (net::RelayStarted()) return;
            net::RelayHeartbeat();
        }
    }).detach();
}

struct RelayJob {
    int seat = 0;
    bool listed = true;
    std::array<char, kRelayAddr> addr{};
    std::array<char, kRelayLink> link{};
    std::array<char, net::RelayClient::kNameLen + 1> name{};
};

bool Endpoint(const char* text, char* host, int host_n, uint16_t* port) {
    if (text && text[0] && ParseAddr(text, host, host_n, port)) return true;
    return net::RelayClient::DefaultRelay(host, host_n, port);
}

void RunRelay(RelayJob job) {
    g_beat.fetch_add(1, std::memory_order_acq_rel);
    char host[kRelayAddr];
    uint16_t port = 0;
    uint8_t token[net::RelayClient::kTokenLen];
    if (job.seat == 1) {
        if (!Endpoint(job.addr.data(), host, int(sizeof host), &port)) {
            PublishRelay("Set the relay address.", nullptr);
            return;
        }
        net::RelayClient::Config cfg;
        cfg.relay_host = host;
        cfg.relay_port = port;
        auto client = std::make_unique<net::RelayClient>(cfg);
        net::RelayClient::OpenInfo info;
        info.listed = job.listed;
        std::snprintf(info.name, sizeof info.name, "%s", job.name[0] ? job.name.data() : "Game");
        char code[net::RelayClient::kCodeLen + 1];
        if (!client->OpenMatch(info, token, code)) {
            if (client->LastFail() == 2) PublishRelay("Relay already has a match.", nullptr);
            else {
                char missing[160];
                std::snprintf(missing, sizeof missing, "No relay at %s:%u. Start relay_server on that port.", host,
                              unsigned(port));
                PublishRelay(missing, nullptr);
            }
            return;
        }
        char link[kRelayLink];
        if (job.addr[0] == '\0') std::snprintf(link, sizeof link, "ff1:%s", code);
        else FormatLink(link, int(sizeof link), host, port, token);
        const char* status = "Hosting. Share the code, or wait for someone to join from the list.";
        if (!det::LockstepEnabled())
            status = "Hosting. The match will not start until the game is launched with --lockstep.";
        else if (det::LockstepPeerSet())
            status = "Hosting. A direct peer is set, so lockstep will not use this relay.";
        PublishRelay(status, link, code);
        net::ArmRelay(1, std::move(client));
        g_relay_seat.store(1, std::memory_order_release);
        if (det::LockstepEnabled() && !det::LockstepPeerSet()) StartBeat();
        return;
    }
    char code[net::RelayClient::kCodeLen + 1];
    bool by_code = ParseCode(job.link.data(), code);
    if (by_code) {
        if (!Endpoint(job.addr.data(), host, int(sizeof host), &port)) {
            PublishRelay("Set the relay address.", nullptr);
            return;
        }
    } else if (!ParseLink(job.link.data(), host, int(sizeof host), &port, token)) {
        PublishRelay("Paste a 6 character code, or ff1:host:port and 32 hex characters.", nullptr);
        return;
    }
    net::RelayClient::Config cfg;
    cfg.relay_host = host;
    cfg.relay_port = port;
    auto client = std::make_unique<net::RelayClient>(cfg);
    int prefer = 0;
    if (by_code) {
        if (const char* seat_env = std::getenv("FUZION_RELAY_SEAT")) prefer = std::atoi(seat_env);
    }
    if (by_code && !client->Resolve(code, token)) {
        PublishRelay("That code is not an open game.", nullptr);
        return;
    }
    int seat = 0;
    if (!client->Join(token, &seat, prefer)) {
        PublishRelay("Relay refused the join.", nullptr);
        return;
    }
    char joined[80];
    std::snprintf(joined, sizeof joined, "Joined as seat %d.", seat);
    if (!det::LockstepEnabled())
        PublishRelay("Joined. The match will not start until the game is launched with --lockstep.", nullptr);
    else if (det::LockstepPeerSet())
        PublishRelay("Joined. A direct peer is set, so lockstep will not use this relay.", nullptr);
    else
        PublishRelay(joined, nullptr);
    net::ArmRelay(seat, std::move(client));
    g_relay_seat.store(seat, std::memory_order_release);
}

void RunList(std::array<char, kRelayAddr> addr) {
    char host[kRelayAddr];
    uint16_t port = 0;
    if (!Endpoint(addr.data(), host, int(sizeof host), &port)) {
        PublishRelay("Set the relay address.", nullptr);
        return;
    }
    net::RelayClient::Game rows[kGameCap];
    int n = net::RelayClient::List(host, port, rows, kGameCap);
    if (n < 0) {
        PublishRelay("The relay did not answer the list.", nullptr);
        return;
    }
    PublishGames(rows, n);
}

void StartMatch() {
    int idle = 0;
    if (!g_relay_busy.compare_exchange_strong(idle, 1, std::memory_order_acq_rel)) return;
    PublishRelay("Starting the match.", nullptr);
    std::thread([] {
        try {
            if (!net::RelayRequestRestart()) PublishRelay("Start failed.", nullptr);
            else PublishRelay("Restarting both games.", nullptr);
        } catch (const std::exception& e) {
            LOGE("relay: start %s", e.what());
            PublishRelay("Start failed.", nullptr);
        } catch (...) {
            LOGE("relay: start failed");
            PublishRelay("Start failed.", nullptr);
        }
        g_relay_busy.store(0, std::memory_order_release);
    }).detach();
}

void StartRelay(int seat, const char* addr, const char* link, bool listed, const char* name) {
    int idle = 0;
    if (!g_relay_busy.compare_exchange_strong(idle, 1, std::memory_order_acq_rel)) return;
    RelayJob job;
    job.seat = seat;
    job.listed = listed;
    std::snprintf(job.addr.data(), job.addr.size(), "%s", addr ? addr : "");
    std::snprintf(job.link.data(), job.link.size(), "%s", link ? link : "");
    std::snprintf(job.name.data(), job.name.size(), "%s", name ? name : "");
    PublishRelay(seat == 1 ? "Opening relay." : "Joining relay.", nullptr);
    std::thread([job] {
        try {
            RunRelay(job);
        } catch (const std::exception& e) {
            LOGE("relay: %s", e.what());
            PublishRelay("Could not reach the relay.", nullptr);
        } catch (...) {
            LOGE("relay: join failed");
            PublishRelay("Could not reach the relay.", nullptr);
        }
        g_relay_busy.store(0, std::memory_order_release);
    }).detach();
}

void StartList(const char* addr) {
    int idle = 0;
    if (!g_relay_busy.compare_exchange_strong(idle, 1, std::memory_order_acq_rel)) return;
    std::array<char, kRelayAddr> copy{};
    std::snprintf(copy.data(), copy.size(), "%s", addr ? addr : "");
    std::thread([copy] {
        try {
            RunList(copy);
        } catch (const std::exception& e) {
            LOGE("relay: list %s", e.what());
            PublishRelay("Could not reach the relay.", nullptr);
        } catch (...) {
            LOGE("relay: list failed");
            PublishRelay("Could not reach the relay.", nullptr);
        }
        g_relay_busy.store(0, std::memory_order_release);
    }).detach();
}

bool KnownRelay() {
    char host[kRelayAddr];
    uint16_t port = 0;
    return net::RelayClient::DefaultRelay(host, int(sizeof host), &port);
}

const char* RelayField() { return KnownRelay() ? "" : g_relay_addr; }

void DrawInternet() {
    char status[kRelayStatus];
    char link[kRelayLink];
    char code[net::RelayClient::kCodeLen + 1];
    ReadRelay(status, int(sizeof status), link, int(sizeof link), code, int(sizeof code));
    if (link[0] && std::strcmp(link, g_relay_seen) != 0) {
        std::snprintf(g_relay_seen, sizeof g_relay_seen, "%s", link);
        std::snprintf(g_link_edit, sizeof g_link_edit, "%s", link);
    }
    if (!KnownRelay()) {
        Row("Relay");
        ImGui::InputText("##relay", g_relay_addr, sizeof g_relay_addr);
        Note("host:port of the relay. Set FUZION_RELAY to hide this.");
    }
    const bool online = det::LockstepEnabled() && !det::LockstepPeerSet();
    if (online) {
        Row("Name");
        ImGui::InputText("##name", g_name_edit, sizeof g_name_edit);
        Row("Private");
        ImGui::Checkbox("Only people with the code", &g_link_only);
    }
    Row("Code");
    ImGui::InputText("##link", g_link_edit, sizeof g_link_edit);
    if (code[0]) {
        ImGui::SetWindowFontScale(1.6f);
        ImGui::TextUnformatted(code);
        ImGui::SetWindowFontScale(1.0f);
    }
    Row("Lobby");
    const bool busy = g_relay_busy.load(std::memory_order_relaxed) != 0;
    const char* field = RelayField();
    ImGui::BeginDisabled(busy);
    if (!det::LockstepEnabled()) {
        if (ImGui::Button("Play online")) app::PostRelay(6, nullptr, g_link_edit);
        Note("Restarts the game so both machines share one simulation. Paste a code first to join that game.");
    } else if (det::LockstepPeerSet()) {
        Note("A direct peer is set. Lockstep keeps using UDP.");
    } else {
        char packed[kRelayAddr + 2];
        std::snprintf(packed, sizeof packed, "%c%s", g_link_only ? 'P' : 'H', field);
        if (ImGui::Button("Host")) app::PostRelay(1, packed, g_name_edit);
        ImGui::SameLine();
        if (ImGui::Button("Join")) app::PostRelay(2, field, g_link_edit);
        if (g_relay_seat.load(std::memory_order_relaxed) == 1) {
            ImGui::SameLine();
            if (ImGui::Button("Start")) app::PostRelay(3, nullptr, nullptr);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(link[0] == '\0');
        if (ImGui::Button("Copy")) app::PostRelay(4, nullptr, link);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) app::PostRelay(5, field, nullptr);
        char players[32];
        std::snprintf(players, sizeof players, "Players %d of 4.", net::RelayCount());
        Note(players);
        Note(net::RelayStarted() ? "The match is starting." : "The host has not started yet.");
        net::RelayClient::Game rows[kGameCap];
        int n = ReadGames(rows, kGameCap);
        if (g_list_known.load(std::memory_order_acquire) != 0 && n == 0) Note("No open games.");
        for (int i = 0; i < n; i++) {
            ImGui::PushID(i);
            char label[48];
            const char* name = rows[i].name[0] ? rows[i].name : "Game";
            std::snprintf(label, sizeof label, "%s  %d/4", name, rows[i].seats);
            Row(label);
            if (ImGui::Button("Join")) app::PostRelay(2, field, rows[i].code);
            ImGui::PopID();
        }
    }
    ImGui::EndDisabled();
    if (status[0]) Note(status);
}

void MaybeJoinCode() {
    static bool once = false;
    if (once) return;
    once = true;
    const char* code = std::getenv("FUZION_RELAY_CODE");
    if (!code || !code[0]) return;
    std::snprintf(g_link_edit, sizeof g_link_edit, "%s", code);
    if (!det::LockstepEnabled() || det::LockstepPeerSet()) return;
    app::PostRelay(2, RelayField(), g_link_edit);
}

void DrawSettings() {
    const ImGuiIO& io = ImGui::GetIO();
    // centred, auto fitting its height, and resizable once it is up
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f}, ImGuiCond_Appearing, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({620.0f * g_ui_scale, 0.0f}, ImGuiCond_Appearing);
    if (g_settings_focus) ImGui::SetNextWindowFocus();
    g_settings_focus = false;
    bool open = true;
    if (!ImGui::Begin("Settings###fuzion_settings", &open,
                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        if (!open) g_settings_open = false;
        return;
    }

    const float label_width = ImGui::CalcTextSize("Anisotropic filtering").x + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SeparatorText("Display");
    if (BeginRows("##display_rows", label_width)) {
        DrawDisplay();
        ImGui::EndTable();
    }
    ImGui::SeparatorText("Quality");
    if (BeginRows("##quality_rows", label_width)) {
        DrawQuality();
        ImGui::EndTable();
    }
    ImGui::SeparatorText("Input");
    if (BeginRows("##input_rows", label_width)) {
        DrawInput();
        ImGui::EndTable();
    }
    ImGui::SeparatorText("Cheats");
    if (cheats::Table().empty()) {
        // the table is empty until a code is found in this image (src/game/cheats.cpp says what is missing)
        Note("No cheats identified for this title yet.");
    } else {
        const bool ingame = cheats::InGame();
        Note(ingame ? "The game's own pad codes, entered as if typed on the pad. Each click turns one on or off, "
                      "and none is saved."
                    : "Codes go in only while the game is running and in a state that takes them.");
        if (BeginRows("##cheat_rows", label_width)) {
            DrawCheats(ingame);
            ImGui::EndTable();
        }
    }
    ImGui::SeparatorText("Internet");
    if (BeginRows("##internet_rows", label_width)) {
        DrawInternet();
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    const float button = ImGui::GetFontSize() * 6.0f;
    if (ImGui::Button("Apply", {button, 0.0f})) {
        Apply();
        Status("Applied: the renderer takes it from the next frame.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Save", {button, 0.0f})) {
        Apply();
        app::Save();
        Status("Applied and written to settings.toml.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Close", {button, 0.0f})) open = false;

    if (g_status_left > 0.0f) {
        g_status_left -= io.DeltaTime;
        ImGui::TextDisabled("%s", g_status);
    } else {
        ImGui::TextDisabled("F1 closes this panel, F2 shows the frame times.");
    }
    ImGui::End();
    if (!open) g_settings_open = false;
}

}  // namespace

void RelayFault(const char* status) { ShowFault(status); }

void RelaunchMatch() {
#if !defined(_WIN32)
    char code[8];
    char relay[80];
    char seat[8];
    if (!net::RelayRejoinInfo(code, int(sizeof code), relay, int(sizeof relay))) return;
    std::snprintf(seat, sizeof seat, "%d", net::RelaySeat());
    setenv("FUZION_RELAY_SEAT", seat, 1);
    setenv("FUZION_RELAY_REJOIN", "1", 1);
    app::RestartOnline(code, relay[0] ? relay : nullptr);
#else
    PublishRelay("Could not restart the match.", nullptr);
#endif
}

void RelayAction(int action, const char* addr, const char* link) {
    if (action == 3) StartMatch();
    else if (action == 5) StartList(addr);
    else if (action == 6) {
        if (!app::RestartOnline(link, nullptr)) PublishRelay("Could not restart the game.", nullptr);
    } else if (action == 1) {
        bool listed = true;
        if (addr && (addr[0] == 'H' || addr[0] == 'P')) {
            listed = addr[0] == 'H';
            addr++;
        }
        StartRelay(1, addr, nullptr, listed, link);
    } else if (action == 2) StartRelay(2, addr, link, true, nullptr);
}

void Draw(double fps) {
    // the renderer calls this inside its frame, but a hook that cannot be reached by a broken
    // context is worth the two compares: this runs on the render thread
    if (!ImGui::GetCurrentContext()) return;
    if (g_fault_panel.exchange(0, std::memory_order_acq_rel) != 0) {
        g_settings_open = true;
        g_settings_up.store(true, std::memory_order_relaxed);
    }
    MaybeJoinCode();
    if (net::RelayWantsRestart()) RelaunchMatch();
    ImGuiIO& io = ImGui::GetIO();

    // the pad belongs to the game alone: no overlay window can ever act on a button the player
    // pressed, whatever the host's ImGui backend feeds it

    // minimized, or before the backend has a size: nothing to draw into
    if (io.DisplaySize.x < 1.0f || io.DisplaySize.y < 1.0f) return;
    ScaleStyle(io);
    Sample(io.DeltaTime);

    // the host feeds every key to ImGui under the same lock, so these land while the game has
    // focus. F3 is the frame capture's
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
        g_settings_open = !g_settings_open;
        if (g_settings_open) {
            SyncDraft();
            g_settings_focus = true;
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) g_perf_open = !g_perf_open;
    // the keyboard-to-pad layer stands down while the panel has the keyboard (input_kbm.cpp)
    g_settings_up.store(g_settings_open, std::memory_order_relaxed);

    // the mouse is only ImGui's while the settings panel is up; closed, the overlay neither
    // captures a click nor touches the cursor, and io.WantCaptureMouse stays false
    if (g_settings_open) io.ConfigFlags &= ~(ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange);
    else io.ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange;

    // the wheel reaches the keyboard-and-mouse pad as a camera zoom pulse (input_kbm.cpp); while
    // the settings panel is up it is the panel's, not the game's
#if !defined(_WIN32)
    // Linux only: on Windows the wheel reaches the pad as WM_MOUSEWHEEL in kbm::Message,
    // and kbm::NoteWheel does not exist there
    if (!g_settings_open && io.MouseWheel != 0.0f) kbm::NoteWheel(int(std::lround(io.MouseWheel)));
#endif

    if (!g_perf_open && !g_settings_open) return;  // closed: the overlay draws nothing else
    if (g_perf_open) DrawPerf(fps);
    if (g_settings_open) DrawSettings();
}

bool SettingsOpen() { return g_settings_up.load(std::memory_order_relaxed); }

}  // namespace overlay
