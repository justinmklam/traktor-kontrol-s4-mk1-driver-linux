// =============================================================================
// tools/replay_evdev_trace/main.cpp
//
// Replay harness for the Traktor S4 driver's input-translation pipeline.
//
// Usage:
//   ./replay_evdev_trace [options] < trace.txt
//
// Trace format (one event per line):
//   <type> <code> <value>
//   e.g.: 1 261 1   (EV_KEY, code 261 = CH1_CUE, pressed)
//         3 52 512  (EV_ABS, code 52 = CH1 jog turn, value 512)
//
// Options:
//   --config <path>   Path to config.json (default: ../config.json)
//   --dump-midi       Print captured MIDI messages as hex
//   --dump-leds       Print captured LED commands
//   --no-report       Suppress the perf-counter summary at exit
//
// Build:
//   cmake -B build && make -C build replay_evdev_trace
// =============================================================================

#include <cstdio>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <csignal>
#include <thread>
#include <getopt.h>
#include <linux/input.h>
#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"

// The translation classes we want to exercise.
#include "EvDevEvent.h"
#include "Button.h"
#include "Knob.h"
#include "Jog.h"
#include "Slider.h"
#include "MidiEventOut.h"
#include "UtilsHelper.h"
#include "ConfigHelper.h"
#include "PerfCounters.h"

// ---------------------------------------------------------------------------
// Global configuration
// ---------------------------------------------------------------------------
static ConfigHelper *g_config = nullptr;
static bool g_dump_midi = false;
static bool g_dump_leds = false;
static bool g_no_report = false;
static int g_event_delay_us = 0;  // delay between events in microseconds

// ---------------------------------------------------------------------------
// Extern: capture collections populated by stubs.cpp
// ---------------------------------------------------------------------------
extern std::vector<std::vector<unsigned char>> g_captured_midi;
extern std::vector<std::tuple<int, int, int>>  g_captured_leds;
extern std::mutex g_capture_mutex;

// ---------------------------------------------------------------------------
// Help
// ---------------------------------------------------------------------------
static void print_usage(const char *prog) {
    std::cerr << "Usage: " << prog << " [options] < trace.txt\n"
              << "Options:\n"
              << "  --config <path>   Path to config.json (default: ../config.json)\n"
              << "  --dump-midi       Print captured MIDI messages\n"
              << "  --dump-leds       Print captured LED commands\n"
              << "  --no-report       Suppress perf-counter summary\n"
              << "  --event-delay-ms <n>  Sleep n ms between events (for jog timing)\n"
              << "  --trace-file <path>   Read trace from file instead of stdin\n"
              << "  --help            Show this message\n";
}

// ---------------------------------------------------------------------------
// Parse a trace line:  "type code value"
// ---------------------------------------------------------------------------
struct input_event parse_trace_line(const std::string &line) {
    struct input_event ev{};
    // Default to EV_KEY (1) if only two tokens — for common button traces
    int type = 1, code = 0, value = 0;
    std::istringstream ss(line);
    std::string token;
    std::vector<int> tokens;
    while (ss >> token) {
        tokens.push_back(std::stoi(token));
    }
    if (tokens.size() >= 3) {
        type  = tokens[0];
        code  = tokens[1];
        value = tokens[2];
    } else if (tokens.size() == 2) {
        code  = tokens[0];
        value = tokens[1];
    } else {
        std::cerr << "Warning: skipping unparseable line: " << line << "\n";
        ev.type = -1; // marker for skip
        return ev;
    }
    ev.type  = static_cast<__u16>(type);
    ev.code  = static_cast<__u16>(code);
    ev.value = static_cast<__s32>(value);
    // ev.time is left as zero — not used by the translation path
    return ev;
}

// ---------------------------------------------------------------------------
// Print a MIDI message as hex bytes
// ---------------------------------------------------------------------------
static void print_midi(const std::vector<unsigned char> &msg) {
    if (msg.empty()) { std::cout << "  (empty)\n"; return; }
    std::cout << "  [ ";
    for (size_t i = 0; i < msg.size(); i++) {
        printf("0x%02x ", msg[i]);
    }
    std::cout << "]\n";
}

// ---------------------------------------------------------------------------
// Print all captured MIDI messages
// ---------------------------------------------------------------------------
static void dump_midi() {
    std::lock_guard<std::mutex> lock(g_capture_mutex);
    if (g_captured_midi.empty()) {
        std::cout << "  No MIDI messages captured.\n";
        return;
    }
    for (size_t i = 0; i < g_captured_midi.size(); i++) {
        std::cout << "  msg #" << i << ":";
        print_midi(g_captured_midi[i]);
    }
}

// ---------------------------------------------------------------------------
// Print all captured LED commands
// ---------------------------------------------------------------------------
static void dump_leds() {
    std::lock_guard<std::mutex> lock(g_capture_mutex);
    if (g_captured_leds.empty()) {
        std::cout << "  No LED commands captured.\n";
        return;
    }
    for (size_t i = 0; i < g_captured_leds.size(); i++) {
        auto [ctl, val, card] = g_captured_leds[i];
        std::cout << "  led #" << i << ": control_id=" << ctl
                  << " value=" << val << " card=" << card << "\n";
    }
}

// ---------------------------------------------------------------------------
// MAIN
// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
    // ---- Parse arguments ----
    std::string config_path = "../config.json";

    // Simple argument parsing
    for (int i = 1; i < argc; i++) {
        std::string arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--dump-midi") {
            g_dump_midi = true;
        } else if (arg == "--dump-leds") {
            g_dump_leds = true;
        } else if (arg == "--event-delay-ms" && i + 1 < argc) {
            g_event_delay_us = std::atoi(argv[++i]) * 1000;
        } else if (arg == "--trace-file" && i + 1 < argc) {
            if (freopen(argv[++i], "r", stdin) == nullptr) {
                std::cerr << "Error: could not open " << argv[i] << "\n";
                return 1;
            }
        } else if (arg == "--no-report") {
            g_no_report = true;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    // ---- Initialise ConfigHelper ----
    g_config = new ConfigHelper();
    if (!g_config->init_config(config_path)) {
        // Fallback: create a minimal in-memory config
        std::cerr << "Warning: could not load " << config_path
                  << ", using built-in defaults.\n";
        // The harness will run with a default ConfigHelper that returns -1/empty
        // for miss — acceptable for mapping-only tests.
    }

    // ---- Initialize spdlog (needed by translation classes) ----
    std::string logger_name = g_config->get_string_value("traktor_s4_logger_name");
    if (logger_name.empty()) logger_name = "replay_harness";
    if (!spdlog::get(logger_name)) {
        try {
            auto console = spdlog::stdout_color_mt(logger_name);
            console->set_level(spdlog::level::warn);
        } catch (...) {
            // Already exists or can't create — continue anyway
        }
    }

    // ---- Read trace from stdin ----
    std::vector<struct input_event> events;
    std::string line;
    while (std::getline(std::cin, line)) {
        // Skip comments and blank lines
        if (line.empty() || line[0] == '#' || line[0] == '/') continue;
        auto ev = parse_trace_line(line);
        if (ev.type == static_cast<__u16>(-1)) continue;
        events.push_back(ev);
    }

    if (events.empty()) {
        std::cerr << "No events read from stdin.\n";
        return 1;
    }

    std::cout << "Replaying " << events.size() << " events...\n\n";

    // ---- Replay through translation pipeline ----
    RtMidiOut *dummy_midi_out = nullptr;  // never dereferenced in replay mode
    int fake_controller_id = 0;
    bool shift_ch1 = false, shift_ch2 = false;
    bool toggle_ac = false, toggle_bd = false;

    for (size_t i = 0; i < events.size(); i++) {
        auto &ev = events[i];

        auto t_start = PerfCounters::now();

        // ---- State updates (mirrors EvDevHelper::read_events_from_device) ----
        // These config lookups are done on every event; we're instrumenting the
        // current behavior, not optimising it.
        if (ev.code == g_config->get_int_value("alsa_device_shift_ch1_value")) {
            shift_ch1 = !shift_ch1;
            std::cout << "[" << i << "] shift_ch1 = " << shift_ch1 << "\n";
            continue;
        }
        if (ev.code == g_config->get_int_value("alsa_device_shift_ch2_value")) {
            shift_ch2 = !shift_ch2;
            std::cout << "[" << i << "] shift_ch2 = " << shift_ch2 << "\n";
            continue;
        }
        if ((ev.code == g_config->get_int_value("alsa_device_toggle_ac_value")) && (ev.value == 1)) {
            toggle_ac = !toggle_ac;
            std::cout << "[" << i << "] toggle_ac = " << toggle_ac << "\n";
            // Note: in full replay the LED bulk writes would happen here,
            // but they are captured by the AlsaHelper stubs.
            continue;
        }
        if ((ev.code == g_config->get_int_value("alsa_device_toggle_bd_value")) && (ev.value == 1)) {
            toggle_bd = !toggle_bd;
            std::cout << "[" << i << "] toggle_bd = " << toggle_bd << "\n";
            continue;
        }

        // ---- Translate ----
        EvDevEvent evdev_event(ev.type, ev.code, ev.value, ev.time);
        evdev_event.handle_with(dummy_midi_out, fake_controller_id,
                                shift_ch1, shift_ch2,
                                toggle_ac, toggle_bd, g_config);

        PerfCounters::record("harness_event_total", t_start);

        // Optional delay between events (needed for jog timing accumulation)
        if (g_event_delay_us > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(g_event_delay_us));
        }
    }

    // ---- Report results ----
    std::cout << "\n=== Results ===\n";
    std::cout << "Events processed: " << events.size() << "\n";

    if (g_dump_midi) {
        std::cout << "\n--- Captured MIDI messages ---\n";
        dump_midi();
    } else {
        std::lock_guard<std::mutex> lock(g_capture_mutex);
        std::cout << "MIDI messages captured: " << g_captured_midi.size() << "\n";
        if (!g_captured_midi.empty()) {
            std::cout << "  First message:";
            print_midi(g_captured_midi.front());
            std::cout << "  Last message:";
            print_midi(g_captured_midi.back());
        }
    }

    if (g_dump_leds) {
        std::cout << "\n--- Captured LED commands ---\n";
        dump_leds();
    } else {
        std::lock_guard<std::mutex> lock(g_capture_mutex);
        std::cout << "LED commands captured: " << g_captured_leds.size() << "\n";
    }

    // ---- Perf counter report ----
    if (!g_no_report) {
        std::cout << "\n";
        PerfCounters::report();
    }

    std::cout << "\nDone.\n";
    return 0;
}
