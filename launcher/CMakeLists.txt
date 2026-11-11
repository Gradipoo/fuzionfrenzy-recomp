cmake_minimum_required(VERSION 3.25)
project(RingHostLauncher LANGUAGES C CXX)

# The Linux and Windows launcher. It builds without a disc image and without
# the recompiled game. The macOS launcher stays a separate SwiftUI app.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # A player should not need the Visual C++ redistributable just to open the
    # launcher. The game it builds still uses the default runtime.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_RENDER ON CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
if(UNIX AND NOT APPLE)
    set(SDL_X11 ON CACHE BOOL "" FORCE)
    set(SDL_WAYLAND ON CACHE BOOL "" FORCE)
    # The launcher does not blank the screen or inject X events. SDL treats a
    # missing library for either of these as a configure error.
    set(SDL_X11_XSCRNSAVER OFF CACHE BOOL "" FORCE)
    set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)
    set(SDL_ALSA OFF CACHE BOOL "" FORCE)
    set(SDL_PULSEAUDIO OFF CACHE BOOL "" FORCE)
    set(SDL_PIPEWIRE OFF CACHE BOOL "" FORCE)
    set(SDL_JACK OFF CACHE BOOL "" FORCE)
    set(SDL_SNDIO OFF CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.16
    GIT_SHALLOW TRUE)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(SDL3 imgui)

add_library(launcher_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
target_include_directories(launcher_imgui SYSTEM PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends)
target_link_libraries(launcher_imgui PRIVATE SDL3::SDL3-static)

add_executable(RingHostLauncher
    src/main.cpp
    src/launcher.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../sdk/runtime/kernel/crypto.cpp)
target_include_directories(RingHostLauncher PRIVATE
    src
    ${CMAKE_CURRENT_LIST_DIR}/../sdk/runtime)
target_link_libraries(RingHostLauncher PRIVATE launcher_imgui SDL3::SDL3-static)

if(WIN32)
    # SHGetKnownFolderPath and CoInitializeEx.
    target_link_libraries(RingHostLauncher PRIVATE shell32 ole32)
endif()

if(UNIX AND NOT APPLE)
    # The launcher itself should run on a stock desktop. SDL is static; the
    # C++ runtime is the only non-glibc library left.
    target_link_options(RingHostLauncher PRIVATE -static-libstdc++ -static-libgcc)
endif()
