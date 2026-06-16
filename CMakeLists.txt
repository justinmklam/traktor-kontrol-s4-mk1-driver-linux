cmake_minimum_required(VERSION 3.16)
project(traktor_kontrol_s4_mk1_driver_linux)

set(CMAKE_CXX_STANDARD 17)

# ---------------------------------------------------------------------------
# Option: enable performance counters (off by default)
# ---------------------------------------------------------------------------
option(ENABLE_PERF_COUNTERS "Enable timing instrumentation in hot paths" OFF)
if(ENABLE_PERF_COUNTERS)
    add_compile_definitions(ENABLE_PERF_COUNTERS)
    message(STATUS "Perf counters ENABLED")
endif()

include_directories(./classes)
include_directories(./includes)

add_compile_options(-lpthread)

# ===========================================================================
# Main driver binary
# ===========================================================================
add_executable(traktor_kontrol_s4_mk1_driver_linux
        includes/Button.h
        includes/EvDevHelper.h
        includes/MidiHelper.h
        includes/Knob.h
        includes/Slider.h
        includes/Led.h
        includes/EvDevEvent.h
        includes/Jog.h
        includes/MidiEventOut.h
        includes/AlsaHelper.h
        includes/ConfigHelper.h
        includes/MidiEventIn.h
        includes/cxxopts.hpp
        includes/UtilsHelper.h
        includes/PerfCounters.h
        classes/Button.cpp
        classes/MidiHelper.cpp
        classes/EvDevHelper.cpp
        classes/Knob.cpp
        classes/Slider.cpp
        classes/Led.cpp
        classes/EvDevEvent.cpp
        classes/Jog.cpp
        classes/MidiEventOut.cpp
        classes/AlsaHelper.cpp
        classes/ConfigHelper.cpp
        classes/MidiEventIn.cpp
        classes/UtilsHelper.cpp
        classes/PerfCounters.cpp
        main.cpp)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PC_EVDEV libevdev REQUIRED)
find_path(EVDEV_INCLUDE_DIR libevdev/libevdev.h
          HINTS ${PC_EVDEV_INCLUDE_DIRS} ${PC_EVDEV_INCLUDEDIR})
find_library(EVDEV_LIBRARY
        NAMES evdev libevdev)
include_directories(${EVDEV_INCLUDE_DIR})
target_link_libraries(traktor_kontrol_s4_mk1_driver_linux rtmidi ${EVDEV_LIBRARY} libasound.so.2 libpthread.so.0)

# ===========================================================================
# Replay harness (no RtMidi / ALSA / libevdev dependencies)
# ===========================================================================
add_executable(replay_evdev_trace
        includes/PerfCounters.h
        classes/PerfCounters.cpp
        classes/EvDevEvent.cpp
        classes/Button.cpp
        classes/Knob.cpp
        classes/Jog.cpp
        classes/Slider.cpp
        classes/MidiEventOut.cpp
        classes/UtilsHelper.cpp
        classes/ConfigHelper.cpp
        classes/Led.cpp
        tools/replay_evdev_trace/main.cpp
        tools/replay_evdev_trace/stubs.cpp)

target_compile_definitions(replay_evdev_trace PRIVATE REPLAY_MODE)
target_compile_definitions(replay_evdev_trace PRIVATE ENABLE_PERF_COUNTERS)
target_include_directories(replay_evdev_trace PRIVATE ./includes ./classes)

# The harness only needs the C++ standard library and pthreads
target_link_libraries(replay_evdev_trace pthread)

# ---------------------------------------------------------------------------
# Custom target to run all test traces through the harness
# Note: jog rotation uses --event-delay-ms because the timing-based
# accumulator won't fire without realistic inter-event spacing.
# ---------------------------------------------------------------------------
add_custom_target(run-replay-traces
        COMMAND echo "=== headphone_press ==="
        COMMAND replay_evdev_trace --dump-midi --config ../config.json --trace-file
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/replay_evdev_trace/test_traces/headphone_press.txt
        COMMAND echo "=== cue_press ==="
        COMMAND replay_evdev_trace --dump-midi --config ../config.json --trace-file
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/replay_evdev_trace/test_traces/cue_press.txt
        COMMAND echo "=== jog_rotation ==="
        COMMAND replay_evdev_trace --dump-midi --event-delay-ms 10 --config ../config.json --trace-file
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/replay_evdev_trace/test_traces/jog_rotation.txt
        COMMAND echo "=== jog_touch ==="
        COMMAND replay_evdev_trace --dump-midi --config ../config.json --trace-file
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/replay_evdev_trace/test_traces/jog_touch.txt
        COMMAND echo "=== deck_toggle_with_jog ==="
        COMMAND replay_evdev_trace --dump-midi --event-delay-ms 10 --config ../config.json --trace-file
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/replay_evdev_trace/test_traces/deck_toggle_with_jog.txt
        DEPENDS replay_evdev_trace
        USES_TERMINAL
        COMMENT "Running all replay test traces through the harness")
