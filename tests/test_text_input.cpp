// A focused text box plus a button used to stop SDL text input on the render
// thread. AppKit aborts that call. The game records the request and applies
// it on the window thread. This runs the same two frames both ways.
#include "app/text_input.h"

#include <SDL3/SDL.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <cstdio>
#include <cstring>
#include <thread>

static int g_ime_calls = 0;
static bool g_saw_stop = false;
static app::TextInputQueue* g_queue = nullptr;
static bool g_queue_only = false;
static void (*g_previous)(ImGuiContext*, ImGuiViewport*, ImGuiPlatformImeData*) = nullptr;

static void OnIme(ImGuiContext* ctx, ImGuiViewport* viewport, ImGuiPlatformImeData* data) {
    if (!data) return;
    g_ime_calls++;
    if (!data->WantVisible) g_saw_stop = true;
    std::fprintf(stderr, "ime want %d\n", data->WantVisible ? 1 : 0);
    if (g_queue_only && g_queue) {
        g_queue->Want(data->WantVisible, static_cast<int>(data->InputPos.x), static_cast<int>(data->InputPos.y),
                      static_cast<int>(data->InputLineHeight));
        return;
    }
    if (g_previous) g_previous(ctx, viewport, data);
}

static ImVec2 g_click(-1.0f, -1.0f);

static void Frame(bool focus_text, bool leave_text) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(320.0f, 240.0f);
    io.DeltaTime = 1.0f / 60.0f;
    if (leave_text && g_click.x >= 0.0f) {
        io.AddMousePosEvent(g_click.x, g_click.y);
        io.AddMouseButtonEvent(0, true);
    }
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(320.0f, 240.0f));
    ImGui::Begin("lobby");
    static char relay[64] = "127.0.0.1:9999";
    static char link[128] = "ff1:127.0.0.1:9999:00112233445566778899aabbccddeeff";
    if (focus_text) ImGui::SetKeyboardFocusHere();
    ImGui::InputText("##relay", relay, sizeof relay);
    ImGui::InputText("##link", link, sizeof link);
    ImGui::Button("Host");
    if (focus_text) {
        ImVec2 a = ImGui::GetItemRectMin();
        ImVec2 b = ImGui::GetItemRectMax();
        g_click = ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    }
    ImGui::SameLine();
    ImGui::Button("Join");
    ImGui::SameLine();
    ImGui::Button("Start");
    ImGui::SameLine();
    ImGui::Button("Copy");
    ImGui::End();
    ImGui::Render();
    if (leave_text) io.AddMouseButtonEvent(0, false);
}

static int Run(bool queued) {
    if (!SDL_Init(SDL_INIT_VIDEO)) return 2;
    SDL_Window* window = SDL_CreateWindow("ime", 320, 240, 0);
    if (!window) return 3;
    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);
    for (int i = 0; i < 8; i++) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {}
        SDL_Delay(15);
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    if (!ImGui_ImplSDL3_InitForOther(window)) return 4;
    app::TextInputQueue queue;
    g_queue = &queue;
    g_queue_only = queued;
    g_ime_calls = 0;
    g_saw_stop = false;
    g_previous = ImGui::GetPlatformIO().Platform_SetImeDataFn;
    ImGui::GetPlatformIO().Platform_SetImeDataFn = OnIme;
    std::thread render([] {
        Frame(true, false);
        Frame(false, false);
        Frame(false, true);
        Frame(false, false);
    });
    render.join();
    std::fprintf(stderr, "ime calls %d\n", g_ime_calls);
    if (queued) queue.Apply(window);
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (g_ime_calls < 2 || !g_saw_stop) return 5;
    return 0;
}

int main(int argc, char** argv) {
    bool queued = !(argc > 1 && std::strcmp(argv[1], "off") == 0);
    if (Run(queued) != 0) {
        std::fprintf(stderr, "FAIL text field plus a button\n");
        return 1;
    }
    std::printf("text field plus a button survived\n");
    return 0;
}
