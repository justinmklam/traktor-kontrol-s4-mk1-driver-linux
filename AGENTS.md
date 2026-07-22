# Traktor Kontrol S4 Mk1 Driver for Linux

C++17 Linux driver that reads evdev events from a Native Instruments Traktor
Kontrol S4 Mk1 controller, translates them to MIDI, and exposes a virtual MIDI
port for Mixxx.  Also handles LED feedback from Mixxx back to the controller's
ALSA controls.

**Stack:** C++17, CMake ≥3.16, RtMidi, ALSA (`libasound2`), `libevdev`,
`spdlog` (bundled), `nlohmann/json` (bundled), `cxxopts` (bundled).

**Entry point:** `main.cpp` — parses CLI args, initialises helpers, runs the
evdev read loop, cleans up on shutdown.

---

## Commands

| Action | Command |
|---|---|
| Configure | `mkdir build && cd build && cmake ..` |
| Build driver | `cd build && cmake --build .` |
| Build replay harness only | `cd build && make replay_evdev_trace` |
| Run all replay traces | `cd build && make run-replay-traces` |
| Run driver | `cd build && ./traktor_kontrol_s4_mk1_driver_linux --logMode console` |
| Enable perf counters | `cd build && cmake .. -DENABLE_PERF_COUNTERS=ON && cmake --build .` |
| Run single trace | `cd build && ./replay_evdev_trace --config ../config.json --trace-file <path>` |

`build/` is gitignored — always recreate or work inside it.

---

## Architecture

### Event flow

```
Controller button/turn  →  /dev/input/event*      [evdev]
                                ↓
                         EvDevHelper::read_events_from_device   [evdev → internal event]
                                ↓
                    ┌───────────┴───────────┐
                    ↓                       ↓
              Button / Knob /           Jog / Slider
              handle_event()           handle_event()
                    ↓                       ↓
              MidiHelper::             MidiHelper::
              enqueue_message()        enqueue_message()
                    ↓                       ↓
              Async MIDI out          ─────────────────  →  Mixxx
              sender thread                                [virtual MIDI port]
                                                                ↓
                                                         MidiHelper::
                                                         midi_in_callback  [MIDI feedback]
                                                                ↓
                                                         LedWriter::enqueue()
                                                         AlsaHelper::set_led_value()
                                                                ↓
                                                         ALSA controls  →  Controller LEDs
```

### Load-bearing modules (`classes/` + `includes/`)

| Module | Role |
|---|---|
| `main.cpp` | CLI arg parsing, signal handling, init + cleanup orchestration |
| `ConfigHelper` | Reads `config.json`, holds runtime configuration (log level, device names, paths) |
| `EvDevHelper` | Opens the evdev device, the main read loop (`read_events_from_device`), translates raw evdev events into Button/Knob/Jog/Slider calls |
| `MidiHelper` | Creates virtual MIDI port, async MIDI out sender thread, inbound MIDI callback for Mixxx feedback |
| `AlsaHelper` | Static helpers for ALSA `snd_ctl` operations: device discovery, LED writes |
| `LedWriter` | Async queue for LED updates from Mixxx feedback, debounces and batches writes |
| `Button` | Maps evdev key codes (256-351) to MIDI CC messages; static `buttons_mapping` |
| `Knob` / `Slider` / `Jog` | Handles `EV_ABS` events → MIDI translation; Jog has a timing-based accumulator |
| `EvDevEvent` | Transparent event struct with implicit `operator int()` for the `switch(event) { case EV_KEY: ... }` dispatch |
| `MidiEventOut` / `MidiEventIn` | MIDI message templates with shift/toggle-aware byte fields |
| `UtilsHelper` | `create_message()` — builds the final MIDI byte vector respecting shift/toggle state |
| `PerfCounters` | Zero-overhead-when-off timing instrumentation for the hot path; `PERF_SCOPE` macro / `record()` / `report()` |
| `tools/replay_evdev_trace/` | Standalone harness that replays text traces through the translation path (no RtMidi/ALSA/libevdev) |

### Configuration

`config.json` at root — ALSA device name, MIDI port name, logging, jog
sensitivity, MIDI beat equivalences, evdev input URI.

### Tracing / testing

- `tools/replay_evdev_trace/test_traces/*.txt` — line-based evdev traces
  (`<type> <code> <value>`).
- `make run-replay-traces` runs all traces through the replay harness.
- `REPLAY_MODE` compile definition makes the harness stub out RtMidi/ALSA/libevdev.

---

## Conventions

- **Formatting:** Google style (`.clang-format: BasedOnStyle: Google`).
- **C++ standard:** C++17.
- **Naming:** PascalCase for types/classes/functions; `snake_case` for local
  variables and member fields; `s_` prefix for static members; trailing `_` for
  private members to distinguish from public ones.
- **Logging:** spdlog async logger via `shared_ptr<spdlog::logger>`; logger name
  from config (`traktor_kontrol_s4_logger`); log-level/format strings use
  `{0}`, `{1}`, … placeholders.
- **Error handling:** Use of `exit(EXIT_FAILURE)` in fatal init paths; no
  exceptions; return codes for recoverable errors.
- **Headers:** Traditional `#ifndef` guards with the full path-style macro name
  (`TRAKTOR_KONTROL_S4_MK1_DRIVER_LINUX_<CLASS>_H`).  Standard headers first,
  then a blank `// ---` comment, then project includes.
- **Testing:** Replay-based tests, not unit tests — traces feed the translation
  pipeline and capture MIDI output.  No C++ unit-test framework.
- **Instrumentation:** `PERF_SCOPE("name")` at the top of hot-path scopes;
  `PerfCounters::record(name, start_us)` for manual timing.  Everything is
  a no-op at `ENABLE_PERF_COUNTERS=OFF` (the default).
- **Shutdown:** SIGINT/SIGTERM/SIGQUIT set `g_shutdown_requested` atomic;
  the evdev read loop uses `poll()` and checks the flag on `EINTR`.

---

## Notes

- **Always build after code changes** — run `cd build && cmake --build .` (or
  `cmake .. && cmake --build .` if the build directory needs reconfiguration)
  after any edit to verify it compiles cleanly before suggesting or committing
  changes.
