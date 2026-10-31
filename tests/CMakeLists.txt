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
xbe_test(test_xinput_packet test_xinput_packet.cpp)
xbe_test(test_arena test_arena.cpp)
target_sources(test_arena PRIVATE "${CMAKE_SOURCE_DIR}/src/det/arena.cpp")

xbe_test(test_scenes test_scenes.cpp)
if(TARGET FuzionFrenzy)
    set(_xbe_scene_bin "$<TARGET_FILE:FuzionFrenzy>")
else()
    set(_xbe_scene_bin "")
endif()

add_test(NAME det_clock COMMAND test_det_clock)
add_test(NAME xinput_packet COMMAND test_xinput_packet)
add_test(NAME arena COMMAND test_arena)
add_test(NAME scenes COMMAND test_scenes ${_xbe_scene_bin})
if(WIN32)
    add_test(NAME fp_digest COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/fp-digest-host.sh")
else()
    add_test(NAME fp_digest COMMAND sh "${CMAKE_SOURCE_DIR}/scripts/fp-digest-host.sh")
endif()
add_test(NAME det_diff COMMAND python3 "${CMAKE_SOURCE_DIR}/scripts/det-diff.py" --self-check)

set_tests_properties(det_clock xinput_packet arena fp_digest det_diff PROPERTIES LABELS fast)
set_tests_properties(scenes PROPERTIES LABELS scenes TIMEOUT 900)
set_tests_properties(fp_digest det_diff PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    TIMEOUT 600)
