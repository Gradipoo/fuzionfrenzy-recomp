if(TARGET test_det_clock)
    return()
endif()

include(CTest)
enable_testing()

set(_xbe_tests_dir "${CMAKE_CURRENT_LIST_DIR}")

function(xbe_test name src)
    add_executable(${name} "${_xbe_tests_dir}/${src}")
    target_include_directories(${name} PRIVATE
        "${CMAKE_SOURCE_DIR}/src")
    target_compile_features(${name} PRIVATE cxx_std_20)
endfunction()

xbe_test(test_det_clock test_det_clock.cpp)
xbe_test(test_run_queue test_run_queue.cpp)
xbe_test(test_xinput_packet test_xinput_packet.cpp)
xbe_test(test_arena test_arena.cpp)
target_sources(test_arena PRIVATE "${CMAKE_SOURCE_DIR}/src/det/arena.cpp")
xbe_test(test_lockstep test_lockstep.cpp)
target_sources(test_lockstep PRIVATE "${CMAKE_SOURCE_DIR}/src/net/lockstep.cpp")
xbe_test(test_port_map test_port_map.cpp)
target_sources(test_port_map PRIVATE "${CMAKE_SOURCE_DIR}/src/net/port_map.cpp")
xbe_test(test_play_online test_play_online.cpp)
target_sources(test_play_online PRIVATE "${CMAKE_SOURCE_DIR}/src/common/play_online.cpp")
if(NOT WIN32 AND NOT APPLE)
    target_link_libraries(test_port_map PRIVATE pthread)
endif()
if(WIN32)
    target_link_libraries(test_lockstep PRIVATE ws2_32)
endif()

xbe_test(test_scenes test_scenes.cpp)
if(TARGET FuzionFrenzy)
    set(_xbe_scene_bin "$<TARGET_FILE:FuzionFrenzy>")
else()
    set(_xbe_scene_bin "")
endif()

add_test(NAME det_clock COMMAND test_det_clock)
add_test(NAME run_queue COMMAND test_run_queue)
add_test(NAME xinput_packet COMMAND test_xinput_packet)
add_test(NAME arena COMMAND test_arena)
add_test(NAME lockstep COMMAND test_lockstep)
add_test(NAME port_map COMMAND test_port_map)
add_test(NAME play_online COMMAND test_play_online)
add_test(NAME scenes COMMAND test_scenes ${_xbe_scene_bin})
if(WIN32)
    add_test(NAME fp_digest COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/fp-digest-host.sh")
else()
    add_test(NAME fp_digest COMMAND sh "${CMAKE_SOURCE_DIR}/scripts/fp-digest-host.sh")
endif()
add_test(NAME det_diff COMMAND python3 "${CMAKE_SOURCE_DIR}/scripts/det-diff.py" --self-check)

set_tests_properties(det_clock run_queue xinput_packet arena lockstep port_map play_online fp_digest det_diff PROPERTIES LABELS fast)

if(NOT WIN32 AND TARGET SDL3::SDL3-static AND DEFINED imgui_SOURCE_DIR)
    add_executable(test_text_input
        "${_xbe_tests_dir}/test_text_input.cpp"
        "${imgui_SOURCE_DIR}/imgui.cpp"
        "${imgui_SOURCE_DIR}/imgui_draw.cpp"
        "${imgui_SOURCE_DIR}/imgui_tables.cpp"
        "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp")
    target_include_directories(test_text_input PRIVATE
        "${CMAKE_SOURCE_DIR}/sdk/runtime"
        "${imgui_SOURCE_DIR}"
        "${imgui_SOURCE_DIR}/backends")
    target_compile_features(test_text_input PRIVATE cxx_std_20)
    target_link_libraries(test_text_input PRIVATE SDL3::SDL3-static)
    add_test(NAME text_input COMMAND test_text_input)
    set_tests_properties(text_input PROPERTIES LABELS ime TIMEOUT 60)
endif()
set_tests_properties(scenes PROPERTIES LABELS scenes TIMEOUT 900)
set_tests_properties(fp_digest det_diff PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    TIMEOUT 600)
