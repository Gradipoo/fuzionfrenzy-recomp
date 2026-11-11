#include "launcher.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "misc/cpp/imgui_stdlib.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#ifdef _WIN32
#include <objbase.h>
#endif

namespace {

enum class Page { Play, Picture, Sound, Controls, Disc };

struct DialogResult {
    std::mutex mu;
    bool ready = false;
    std::string path;
};

struct KeyRow {
    const char* key;
    const char* pad;
};

// Same rows as the Mac controls page, which copies the map in src/common/input_kbm.cpp.
const KeyRow kKeys[] = {
    {"W A S D", "left stick"},
    {"arrows", "D-pad"},
    {"mouse, right button", "right stick"},
    {"Q / E", "right stick X"},
    {"R / F", "right stick Y"},
    {"wheel", "right stick Y"},
    {"space", "A"},
    {"esc, backspace", "B"},
    {"X / Y", "X / Y"},
    {"Z / C", "triggers"},
    {"1 / 2", "black / white"},
    {"enter", "Start"},
    {"tab", "Back"},
    {"shift / ctrl", "stick click"},
};

const char* kModes[] = {"borderless", "fullscreen", "windowed"};

struct SizeChoice {
    int width;
    int height;
    const char* label;
};

const SizeChoice kSizes[] = {
    {0, 0, "desktop"},
    {1280, 720, "1280 × 720"},
    {1280, 800, "1280 × 800"},
    {1600, 900, "1600 × 900"},
    {1920, 1080, "1920 × 1080"},
    {1920, 1200, "1920 × 1200"},
    {2560, 1440, "2560 × 1440"},
    {2560, 1080, "2560 × 1080"},
    {3440, 1440, "3440 × 1440"},
    {3840, 2160, "3840 × 2160"},
};

struct CapChoice {
    int cap;
    const char* label;
};

const CapChoice kCaps[] = {
    {30, "30 fps · low power"},
    {60, "60 fps"},
    {0, "unlimited"},
    {72, "72 fps"},
    {120, "120 fps"},
    {144, "144 fps"},
    {165, "165 fps"},
    {240, "240 fps"},
};

const int kRenderSteps[] = {50, 75, 100, 125, 150, 200};
const char* kAspects[] = {"auto", "4:3", "16:9", "16:10", "21:9", "32:9"};
const int kAniso[] = {0, 2, 4, 8, 16};

void OnDialog(void* userdata, const char* const* filelist, int) {
    auto* result = static_cast<DialogResult*>(userdata);
    std::string path;
    if (filelist && filelist[0]) path = filelist[0];
    std::lock_guard<std::mutex> lock(result->mu);
    result->path = std::move(path);
    result->ready = true;
}

std::string ReadTrimmed(const rh::fs::path& path) {
    std::ifstream in(path);
    if (!in) return {};
    std::string line;
    std::getline(in, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
    return line;
}

bool RequiredToolsReady(const std::vector<rh::Prereq>& prereqs) {
    for (const rh::Prereq& item : prereqs) {
        if (item.required && !item.ok) return false;
    }
    return true;
}

bool ToolsReady(const std::vector<rh::Prereq>& prereqs) {
    for (const rh::Prereq& item : prereqs) {
        if (!item.ok) return false;
    }
    return true;
}

std::string Lower(std::string text) {
    for (char& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return text;
}

std::string PrettyPath(const std::string& path) {
    const char* home = std::getenv("HOME");
#ifdef _WIN32
    if (!home || !home[0]) home = std::getenv("USERPROFILE");
#endif
    if (!home || !home[0]) return path;
    const std::string prefix(home);
    if (path.rfind(prefix, 0) != 0) return path;
    return "~" + path.substr(prefix.size());
}

void Note(const char* text) {
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 480.f);
    ImGui::TextDisabled("%s", text);
    ImGui::PopTextWrapPos();
}

void GroupLabel(const char* title) {
    ImGui::Dummy(ImVec2(0.f, 8.f));
    ImGui::TextDisabled("%s", title);
}

bool Choice(const char* id, const char* label, bool on) {
    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_Text, on ? ImVec4(0.93f, 0.94f, 0.96f, 1.f)
                                            : ImVec4(0.55f, 0.64f, 0.78f, 0.85f));
    const bool clicked = ImGui::Selectable(label, on);
    ImGui::PopStyleColor();
    ImGui::PopID();
    return clicked;
}

void DiscField(std::string& disc_path) {
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(width > 520.f ? 520.f : width);
    ImGui::InputText("##disc", &disc_path);
}

}  // namespace

int main(int, char**) {
#ifdef _WIN32
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool com_ready = SUCCEEDED(com);
#endif

    const rh::fs::path exe_dir = rh::ExeDir();
    const rh::fs::path port_source = exe_dir / "port";
    const bool port_ok = rh::fs::exists(port_source / "build.sh") || rh::fs::exists(port_source / "build.bat");
    std::string version = ReadTrimmed(exe_dir / "port-version");
    if (version.empty()) version = "dev";

    const rh::fs::path support = rh::DefaultSupportDir();
    std::error_code ec;
    rh::fs::create_directories(support, ec);
    const rh::fs::path disc_file = support / "disc-path";
    const rh::fs::path settings_file = support / "launcher-settings.toml";

    rh::Settings settings = rh::Settings::Load(settings_file);
    std::string disc_path = ReadTrimmed(disc_file);
    rh::DiscVerdict verdict;
    std::vector<rh::Prereq> prereqs = rh::ScanPrereqs();
    std::string error;
    std::vector<std::string> log;
    if (!disc_path.empty()) {
        verdict = rh::InspectDisc(rh::FromUtf8(disc_path));
        if (!verdict.ok) error = verdict.title + "\n" + verdict.detail;
    }

    auto save_disc = [&] {
        std::ofstream out(disc_file, std::ios::binary | std::ios::trunc);
        if (out) out << disc_path << "\n";
    };
    auto save_settings = [&] { settings.Save(settings_file); };
    auto refresh_disc = [&] {
        if (disc_path.empty()) {
            verdict = {};
            error.clear();
            return;
        }
        verdict = rh::InspectDisc(rh::FromUtf8(disc_path));
        if (!verdict.ok) error = verdict.title + "\n" + verdict.detail;
        else error.clear();
    };

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
#ifdef _WIN32
        if (com_ready) CoUninitialize();
#endif
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Ring Host", 1100, 680, SDL_WINDOW_RESIZABLE);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
#ifdef _WIN32
        if (com_ready) CoUninitialize();
#endif
        return 1;
    }
    SDL_SetWindowMinimumSize(window, 960, 560);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
#ifdef _WIN32
        if (com_ready) CoUninitialize();
#endif
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(28.f, 20.f);
    style.FramePadding = ImVec2(8.f, 6.f);
    style.ItemSpacing = ImVec2(10.f, 4.f);
    style.WindowBorderSize = 0.f;
    style.ChildBorderSize = 0.f;
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.05f, 0.06f, 1.f);
    colors[ImGuiCol_Text] = ImVec4(0.93f, 0.94f, 0.96f, 1.f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.64f, 0.78f, 0.9f);
    colors[ImGuiCol_Header] = ImVec4(1.f, 1.f, 1.f, 0.08f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(1.f, 1.f, 1.f, 0.14f);
    colors[ImGuiCol_HeaderActive] = ImVec4(1.f, 1.f, 1.f, 0.18f);
    colors[ImGuiCol_FrameBg] = ImVec4(1.f, 1.f, 1.f, 0.06f);
    colors[ImGuiCol_Button] = ImVec4(1.f, 1.f, 1.f, 0.06f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(1.f, 1.f, 1.f, 0.12f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.98f, 0.86f, 0.02f, 1.f);
    colors[ImGuiCol_PlotHistogramHovered] = colors[ImGuiCol_PlotHistogram];
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    DialogResult dialog;
    rh::Pipeline pipeline;
    rh::Progress progress;
    bool busy = false;
    bool can_play = false;
    Page page = Page::Play;
    bool running = true;

    auto choose_file = [&] {
        const SDL_DialogFileFilter filters[] = {
            {"Disc image", "iso;xiso"},
            {"All files", "*"},
        };
        SDL_ShowOpenFileDialog(OnDialog, &dialog, window, filters, 2, nullptr, false);
    };
    auto choose_folder = [&] { SDL_ShowOpenFolderDialog(OnDialog, &dialog, window, nullptr, false); };
    auto start = [&](bool rebuild) {
        error.clear();
        log.clear();
        prereqs = rh::ScanPrereqs();
        refresh_disc();
        if (!verdict.ok || !RequiredToolsReady(prereqs)) return;
        save_disc();
        save_settings();
        rh::Job job;
        job.disc = verdict.image ? verdict.file : verdict.folder;
        job.disc_is_image = verdict.image;
        job.support = support;
        job.port_source = port_source;
        job.version = version;
        job.settings_toml = settings.Toml();
        job.mute = !settings.sound;
        job.autostart = settings.scripted_pad;
        job.force_rebuild = rebuild;
        if (!pipeline.Start(job)) error = "A build is already running.";
    };
    auto draw_tools = [&] {
        for (const rh::Prereq& item : prereqs) {
            const std::string title = Lower(item.title);
            ImGui::PushStyleColor(ImGuiCol_Text, item.ok ? ImVec4(0.55f, 0.64f, 0.78f, 0.85f)
                                                         : ImVec4(0.98f, 0.86f, 0.02f, 1.f));
            ImGui::TextUnformatted(title.c_str());
            ImGui::PopStyleColor();
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 480.f);
            ImGui::TextDisabled("%s", item.detail.c_str());
            ImGui::PopTextWrapPos();
        }
        const std::string hint = rh::InstallHint();
        if (!hint.empty()) Note(hint.c_str());
        if (ImGui::Button("check tools again")) prereqs = rh::ScanPrereqs();
    };
    auto draw_disc_chooser = [&](bool enabled) {
        ImGui::BeginDisabled(!enabled);
        DiscField(disc_path);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            refresh_disc();
            save_disc();
        }
        if (ImGui::Selectable("choose file", false)) choose_file();
        if (ImGui::Selectable("choose folder", false)) choose_folder();
        ImGui::EndDisabled();
    };

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) running = false;
        }

        {
            std::lock_guard<std::mutex> lock(dialog.mu);
            if (dialog.ready) {
                dialog.ready = false;
                if (!dialog.path.empty()) {
                    disc_path = dialog.path;
                    refresh_disc();
                    save_disc();
                }
            }
        }

        for (const std::string& line : pipeline.DrainLog()) {
            log.push_back(line);
            if (log.size() > 400) log.erase(log.begin(), log.end() - 400);
        }
        progress = pipeline.Snapshot();
        if (!progress.error.empty()) error = progress.error;
        busy = progress.busy || pipeline.Busy();
        can_play = port_ok && verdict.ok && RequiredToolsReady(prereqs) && !busy;

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Ring Host", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

        float status_h = 8.f;
        if (!port_ok) status_h += 36.f;
        if (verdict.ok && !error.empty()) status_h += 48.f;
        if (busy || !log.empty()) status_h += 132.f;
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        float body_h = avail.y - status_h;
        if (body_h < 80.f) body_h = 80.f;

        if (!verdict.ok) {
            ImGui::BeginChild("welcome", ImVec2(0.f, body_h), ImGuiChildFlags_None);
            ImGui::TextUnformatted("choose disc");
            draw_disc_chooser(true);
            Note("Point this at your own USA disc image, or at a folder that already holds the extracted disc. "
                 "The executable on its own is not enough. This app does not include the game.");
            if (!error.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.86f, 0.02f, 1.f));
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 520.f);
                ImGui::TextUnformatted(error.c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }
            if (!ToolsReady(prereqs)) {
                ImGui::Dummy(ImVec2(0.f, 8.f));
                draw_tools();
            }
            ImGui::EndChild();
        } else {
            ImGui::BeginChild("menu", ImVec2(220.f, body_h), ImGuiChildFlags_None);
            const Page pages[] = {Page::Play, Page::Picture, Page::Sound, Page::Controls, Page::Disc};
            const char* names[] = {"play", "picture", "sound", "controls", "disc"};
            for (int i = 0; i < 5; ++i) {
                const bool on = page == pages[i];
                ImGui::PushStyleColor(ImGuiCol_Text, on ? ImVec4(0.93f, 0.94f, 0.96f, 1.f)
                                                        : ImVec4(0.55f, 0.64f, 0.78f, 0.85f));
                if (ImGui::Selectable(names[i], on)) page = pages[i];
                ImGui::PopStyleColor();
            }
            ImGui::EndChild();
            ImGui::SameLine(0.f, 48.f);
            ImGui::BeginChild("detail", ImVec2(0.f, body_h), ImGuiChildFlags_None);
            if (page == Page::Play) {
                ImGui::TextDisabled("%s", verdict.confirmed ? "usa release" : "disc image");
                Note(verdict.detail.c_str());
                std::string play_label = "play";
                if (progress.running) play_label = "running";
                else if (busy) play_label = Lower(progress.text);
                ImGui::Dummy(ImVec2(0.f, 8.f));
                ImGui::PushID("play");
                ImGui::BeginDisabled(!can_play);
                if (ImGui::Selectable(play_label.c_str(), true)) start(false);
                ImGui::EndDisabled();
                ImGui::PopID();
                Note(progress.running
                         ? "The game is open. Settings were written beside it."
                         : "The first play compiles from your disc on this machine. Later plays reuse that build.");
            } else if (page == Page::Picture) {
                GroupLabel("window");
                for (const char* mode : kModes) {
                    if (Choice(mode, mode, settings.display_mode == mode)) {
                        settings.display_mode = mode;
                        save_settings();
                    }
                }
                Note(settings.display_mode == "borderless"
                         ? "Borderless follows the display. The size below is kept for the other modes."
                         : "Windowed and fullscreen use the size below.");

                GroupLabel("size");
                for (const SizeChoice& size : kSizes) {
                    const bool on = settings.width == size.width && settings.height == size.height;
                    if (Choice(size.label, size.label, on)) {
                        settings.width = size.width;
                        settings.height = size.height;
                        save_settings();
                    }
                }

                GroupLabel("frame cap");
                for (const CapChoice& cap : kCaps) {
                    if (Choice(cap.label, cap.label, settings.fps_cap == cap.cap)) {
                        settings.fps_cap = cap.cap;
                        save_settings();
                    }
                }
                Note("30 is the cooler cap. 60 is the game's pace.");

                GroupLabel("vsync");
                if (Choice("vsync-on", "on", settings.vsync)) {
                    settings.vsync = true;
                    save_settings();
                }
                if (Choice("vsync-off", "off", !settings.vsync)) {
                    settings.vsync = false;
                    save_settings();
                }

                GroupLabel("render scale");
                for (int step : kRenderSteps) {
                    char label[16];
                    SDL_snprintf(label, sizeof(label), "%d%%", step);
                    if (Choice(label, label, settings.render_percent == step)) {
                        settings.render_percent = step;
                        save_settings();
                    }
                }

                GroupLabel("aspect");
                for (const char* aspect : kAspects) {
                    if (Choice(aspect, aspect, settings.aspect == aspect)) {
                        settings.aspect = aspect;
                        save_settings();
                    }
                }

                GroupLabel("ultrawide flag");
                if (Choice("wide-on", "on", settings.ultrawide)) {
                    settings.ultrawide = true;
                    save_settings();
                }
                if (Choice("wide-off", "off", !settings.ultrawide)) {
                    settings.ultrawide = false;
                    save_settings();
                }
                Note("Stored for the game. Extra-wide drawing stays off unless SIMSBO_HORPLUS is set.");

                GroupLabel("anisotropic");
                for (int value : kAniso) {
                    char label[32];
                    char id[32];
                    if (value == 0) SDL_snprintf(label, sizeof(label), "as the game asks");
                    else SDL_snprintf(label, sizeof(label), "%dx", value);
                    SDL_snprintf(id, sizeof(id), "aniso-%d", value);
                    if (Choice(id, label, settings.aniso == value)) {
                        settings.aniso = value;
                        save_settings();
                    }
                }

                GroupLabel("not on this mac");
                Note("HDR is a Windows setting and stays off. The disc's movies are 640×480. There is no 4K movie option.");
            } else if (page == Page::Sound) {
                GroupLabel("sound");
                if (Choice("sound-on", "on", settings.sound)) {
                    settings.sound = true;
                    save_settings();
                }
                if (Choice("sound-off", "off", !settings.sound)) {
                    settings.sound = false;
                    save_settings();
                }
                Note("Off silences the game. This port has no volume slider.");

                GroupLabel("scripted pad");
                if (Choice("pad-off", "off", !settings.scripted_pad)) {
                    settings.scripted_pad = false;
                    save_settings();
                }
                if (Choice("pad-on", "on", settings.scripted_pad)) {
                    settings.scripted_pad = true;
                    save_settings();
                }
                Note("Leave this off to play. On, the port's self-test drives Start, Up, and A through the intro, then walks. "
                     "You skip the intro yourself with Start or Enter.");

                GroupLabel("system-link host");
                ImGui::SetNextItemWidth(280.f);
                if (ImGui::InputTextWithHint("##join", "address", &settings.join_ip)) save_settings();
                Note("Empty stays on the local network.");
            } else if (page == Page::Controls) {
                Note("A controller is an Xbox pad, in the order controllers are plugged in. This port has no button remapper. "
                     "The keyboard is fixed.");
                ImGui::Dummy(ImVec2(0.f, 8.f));
                if (ImGui::BeginTable("keys", 2, ImGuiTableFlags_SizingStretchProp)) {
                    for (const KeyRow& row : kKeys) {
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("%s   %s", row.key, row.pad);
                    }
                    ImGui::EndTable();
                }
            } else {
                if (!disc_path.empty()) Note(PrettyPath(disc_path).c_str());
                ImGui::TextUnformatted("change disc");
                draw_disc_chooser(!busy);
                ImGui::BeginDisabled(!can_play);
                if (ImGui::Selectable("rebuild", false)) start(true);
                ImGui::EndDisabled();
                ImGui::Dummy(ImVec2(0.f, 8.f));
                if (ToolsReady(prereqs)) Note("Tools are on this machine.");
                else draw_tools();
            }
            ImGui::EndChild();
        }

        if (!port_ok) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.86f, 0.02f, 1.f));
            ImGui::TextWrapped("%s", "This copy of the launcher is missing its port sources (port/ beside the executable).");
            ImGui::PopStyleColor();
        }
        if (verdict.ok && !error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.86f, 0.02f, 1.f));
            ImGui::PushTextWrapPos(0.f);
            ImGui::TextUnformatted(error.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        if (busy || !log.empty()) {
            const std::string stage = Lower(progress.text);
            ImGui::TextUnformatted(stage.c_str());
            ImGui::SameLine();
            const float pct_w = progress.fraction >= 0 ? 48.f : 0.f;
            float bar_w = ImGui::GetContentRegionAvail().x - pct_w;
            if (bar_w < 40.f) bar_w = 40.f;
            const float shown = progress.fraction >= 0 ? static_cast<float>(progress.fraction) : (busy ? 0.08f : 0.f);
            ImGui::ProgressBar(shown, ImVec2(bar_w, 4.f), "");
            if (progress.fraction >= 0) {
                ImGui::SameLine();
                ImGui::Text("%d%%", static_cast<int>(progress.fraction * 100.0));
            }
            ImGui::BeginChild("log", ImVec2(0.f, 96.f), ImGuiChildFlags_None);
            for (const std::string& line : log) ImGui::TextUnformatted(line.c_str());
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.f);
            ImGui::EndChild();
        }

        ImGui::End();
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 18, 18, 20, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
#ifdef _WIN32
    if (com_ready) CoUninitialize();
#endif
    return 0;
}
