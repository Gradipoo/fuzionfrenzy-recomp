#include "launcher.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "misc/cpp/imgui_stdlib.h"

#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#ifdef _WIN32
#include <objbase.h>
#endif

namespace {

struct DialogResult {
    std::mutex mu;
    bool ready = false;
    std::string path;
};

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
    if (!disc_path.empty()) verdict = rh::InspectDisc(rh::FromUtf8(disc_path));
    std::vector<rh::Prereq> prereqs = rh::ScanPrereqs();
    std::string error;
    std::vector<std::string> log;

    auto save_disc = [&] {
        std::ofstream out(disc_file, std::ios::binary | std::ios::trunc);
        if (out) out << disc_path << "\n";
    };
    auto save_settings = [&] { settings.Save(settings_file); };
    auto refresh_disc = [&] {
        if (disc_path.empty()) {
            verdict = {};
            verdict.title = "No disc chosen";
            verdict.detail = "Choose an .iso, or a folder that contains the extracted disc.";
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

    SDL_Window* window = SDL_CreateWindow("Ring Host", 960, 640, SDL_WINDOW_RESIZABLE);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
#ifdef _WIN32
        if (com_ready) CoUninitialize();
#endif
        return 1;
    }
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
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    DialogResult dialog;
    rh::Pipeline pipeline;
    bool running = true;

    const char* modes[] = {"borderless", "fullscreen", "windowed"};

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
        const rh::Progress progress = pipeline.Snapshot();
        if (!progress.error.empty()) error = progress.error;

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Ring Host", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

        ImGui::TextUnformatted("Ring Host");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", version.c_str());

        if (!port_ok) {
            ImGui::TextColored(ImVec4(1.f, 0.75f, 0.2f, 1.f),
                               "This copy of the launcher is missing its port sources (port/ beside the executable).");
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("Disc");
        ImGui::SetNextItemWidth(-1.f);
        ImGui::InputText("##disc", &disc_path);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            refresh_disc();
            save_disc();
        }
        if (ImGui::Button("Choose file")) {
            const SDL_DialogFileFilter filters[] = {
                {"Disc image", "iso;xiso"},
                {"All files", "*"},
            };
            SDL_ShowOpenFileDialog(OnDialog, &dialog, window, filters, 2, nullptr, false);
        }
        ImGui::SameLine();
        if (ImGui::Button("Choose folder")) {
            SDL_ShowOpenFolderDialog(OnDialog, &dialog, window, nullptr, false);
        }

        if (!verdict.title.empty()) ImGui::TextWrapped("%s", verdict.title.c_str());
        if (!verdict.detail.empty()) ImGui::TextWrapped("%s", verdict.detail.c_str());

        bool missing_tool = false;
        for (const rh::Prereq& item : prereqs) {
            if (item.ok) continue;
            missing_tool = missing_tool || item.required;
            ImGui::TextWrapped("%s: %s", item.title.c_str(), item.detail.c_str());
        }
        if (missing_tool) {
            const std::string hint = rh::InstallHint();
            ImGui::TextWrapped("%s", hint.c_str());
            if (ImGui::Button("Check tools again")) prereqs = rh::ScanPrereqs();
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("Picture");
        int mode_index = 0;
        for (int i = 0; i < 3; ++i) {
            if (settings.display_mode == modes[i]) mode_index = i;
        }
        ImGui::SetNextItemWidth(220.f);
        if (ImGui::Combo("Display", &mode_index, modes, 3)) {
            settings.display_mode = modes[mode_index];
            save_settings();
        }
        if (ImGui::Checkbox("Vsync", &settings.vsync)) save_settings();
        ImGui::SameLine();
        if (ImGui::Checkbox("Sound", &settings.sound)) save_settings();
        ImGui::SetNextItemWidth(160.f);
        if (ImGui::InputInt("FPS cap (0 is unlimited)", &settings.fps_cap)) {
            if (settings.fps_cap < 0) settings.fps_cap = 0;
            save_settings();
        }

        ImGui::Spacing();
        const bool busy = progress.busy || pipeline.Busy();
        const bool can_play = port_ok && verdict.ok && RequiredToolsReady(prereqs) && !busy;
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
        ImGui::BeginDisabled(!can_play);
        if (ImGui::Button("Play")) start(false);
        ImGui::SameLine();
        if (ImGui::Button("Rebuild")) start(true);
        ImGui::EndDisabled();

        if (busy) ImGui::TextUnformatted(progress.text.c_str());
        else if (progress.running) ImGui::TextUnformatted("The game is open. Settings were written beside it.");
        else ImGui::TextUnformatted(progress.text.empty() ? "Ready" : progress.text.c_str());
        if (progress.fraction >= 0) ImGui::ProgressBar(static_cast<float>(progress.fraction), ImVec2(-1.f, 0.f));
        if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());

        ImGui::Spacing();
        ImGui::BeginChild("log", ImVec2(0, 0), ImGuiChildFlags_Borders);
        for (const std::string& line : log) ImGui::TextUnformatted(line.c_str());
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.f);
        ImGui::EndChild();

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
