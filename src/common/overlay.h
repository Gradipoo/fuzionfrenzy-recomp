#pragma once

// the in-game overlay: F1 settings, F2 performance (overlay.cpp)
namespace overlay {

// app::GameInfo::ui: the render thread, each frame inside the ImGui frame, app::UiLock held
void Draw(double fps);

// any thread: the settings panel is up, so the overlay owns the keyboard and the mouse and the
// keyboard-to-pad layer (input_kbm.cpp) must keep its hands off them
bool SettingsOpen();

// Window thread. 1 host, 2 join, 3 start. addr and link are the fields from the panel.
void RelayAction(int action, const char* addr, const char* link);

}  // namespace overlay
