## Traktor Kontrol S4 Mk1 v0.1 Driver for Linux

Linux driver based on the awesome work from blaxpot and Javi Fonseca on the Traktor Kontrol S4 Mk1 for work with Mixxx.

[traktor-s4-mk1-midify](https://github.com/blaxpot/traktor-s4-mk1-midify)

[Thread from the Mixxx forum](https://mixxx.discourse.group/t/native-instruments-kontrol-s4-v0-1/11942)

## Requirements
- **[RtMidi Library](https://www.music.mcgill.ca/~gary/rtmidi/)**
- **[ALSA Library](https://www.alsa-project.org/wiki/Main_Page)**
- **snd-usb-caiaq** Kernel Module

## Usage

This driver reads events from the Traktor Kontrol S4 Mk1 evdev device,
translates them to MIDI, and exposes a virtual MIDI port that Mixxx can
connect to.  It also handles LED feedback from Mixxx and writes it to the
controller's ALSA controls.

### Quick start

1. Plug in the controller via USB.
2. Ensure the `snd-usb-caiaq` kernel module is loaded (check with `lsmod | grep snd_usb_caiaq`).
3. Run the driver:

```
./traktor_kontrol_s4_mk1_driver_linux --logMode console
```

4. Start Mixxx, go to *Preferences → Controllers*, and the **Traktor Kontrol S4 MK1**
   should appear.  Enable it — the driver's virtual MIDI port is named
   `Traktor Kontrol S4 MK1`.  Mixxx connects to it automatically.

### Command-line options

| Flag | Default | Description |
|---|---|---|
| `--help` | | Show help and exit |
| `--logMode` | `logfile` | Log destination: `console` or `logfile` |
| `--logLevel` | `info` | Verbosity: `info` or `debug` |
| `--configFile` | `../config.json` | Path to the JSON configuration file |

### Configuration file (`config.json`)

The file defines evdev button codes, ALSA device name, MIDI port names, LED
IDs, and jog sensitivity.  See the bundled `config.json` for the full schema.
Key entries:

| Key | Purpose |
|---|---|
| `alsa_device_shift_ch1_value` | evdev code for CH1 shift (default `257`) |
| `alsa_device_toggle_ac_value` | evdev code for deck A/C toggle (default `264`) |
| `jog_wheel_sensitivity` | Jog accumulation window in ms (default `5`) |
| `midi_virtual_port_name` | Virtual MIDI port name Mixxx sees |

### Workflow

1. **Build** the driver (see below).
2. **Run** from the `build/` directory:

```
cd build
./traktor_kontrol_s4_mk1_driver_linux --logMode console
```

   You should see startup logs confirming ALSA device init, MIDI port creation,
   and evdev device detection.

3. **Start Mixxx**.  The driver creates an ALSA MIDI port; Mixxx auto-detects it.
   Load the provided mappings from `mappings/mixxx/`:
   - `TraktorKontrol_S4_MK1_JFM.midi.xml`
   - `Traktor-Kontrol-S4-mk1.js`

4. **Press keys, turn jog wheels.**  MIDI messages flow to Mixxx; Mixxx sends
   LED feedback back to the controller.

### Logging

In console mode, debug-level logging shows every evdev event and MIDI message:

```
cd build
./traktor_kontrol_s4_mk1_driver_linux --logLevel debug --logMode console
```

In file mode (the default), logs are written to the path in
`traktor_s4_log_file` (default `/tmp/traktor_s4/traktor_kontrol_s4_logger.log`).

### Getting performance data

Rebuild with performance counters enabled to get per-operation latency
statistics at shutdown:

```
cmake .. -DENABLE_PERF_COUNTERS=ON
cmake --build .
./traktor_kontrol_s4_mk1_driver_linux --logMode console
# … use the controller for a while, then Ctrl+C
```

See the **Performance counters** section below for the output format.

## Build
1. Install dependencies. The package names will depend on your Linux distribution.

**Debian/Ubuntu/Raspberry Pi OS**
```
sudo apt install cmake cmake-extras libasound2 librtaudio-dev librtmidi-dev libevdev-dev
```

**Arch Linux**
```
sudo pacman -Syu base-devel cmake alsa-lib rtaudio rtmidi libevdev
```

2. Build with `cmake`:
```
mkdir build
cd build
cmake ..
cmake --build .
```

If all goes well, you should now be able to start the application like so:
```
# ./traktor_kontrol_s4_mk1_driver_linux --logMode console
[2023-01-20 09:51:40.493] [traktor_kontrol_s4_logger] [info] [main::init_application] Traktor Kontrol S4 Mk1 Driver for Linux started
[2023-01-20 09:51:40.493] [traktor_kontrol_s4_logger] [info] [main:show_main_configuration] Current arguments:
[2023-01-20 09:51:40.493] [traktor_kontrol_s4_logger] [info] [main:show_main_configuration] Log level: INFO
[2023-01-20 09:51:40.493] [traktor_kontrol_s4_logger] [info] [main:show_main_configuration] Log mode: LOG IN CONSOLE
[2023-01-20 09:51:40.493] [traktor_kontrol_s4_logger] [info] [main::init_application] Starting helpers....
[2023-01-20 09:51:40.503] [traktor_kontrol_s4_logger] [info] [main::init_application] Get MIDI information....
[2023-01-20 09:51:40.505] [traktor_kontrol_s4_logger] [info] [main::init_application] Initializing EvDev device....
```

## Performance counters

Timing instrumentation for the evdev → MIDI/LED translation path can be enabled
at build time. Counters track min/avg/max latency for evdev reads, MIDI
translation and enqueue, ALSA LED writes, and the MIDI feedback callback.

```
mkdir build && cd build
cmake .. -DENABLE_PERF_COUNTERS=ON
cmake --build .
```

At runtime, pressing Ctrl+C (SIGINT) prints a summary like:

```
[2026-06-16 16:49:04.681] [traktor_kontrol_s4_logger] [info] === PerfCounters report ===
[2026-06-16 16:49:04.681] [traktor_kontrol_s4_logger] [info]   midi_sendMessage                          count=   17206  min=     0µs  avg=     1µs  max=    29µs  total=     17866µs
[2026-06-16 16:49:04.681] [traktor_kontrol_s4_logger] [info]   evdev_translate_and_enqueue               count=   52278  min=     0µs  avg=     1µs  max=    39µs  total=    101005µs
[2026-06-16 16:49:04.681] [traktor_kontrol_s4_logger] [info]   evdev_next_event                          count=51738834  min=     0µs  avg=     0µs  max=    71µs  total=   8799493µs
[2026-06-16 16:49:04.681] [traktor_kontrol_s4_logger] [info]   alsa_set_led_value                        count=      41  min=     9µs  avg=    66µs  max=   146µs  total=      2706µs
[2026-06-16 16:49:04.681] [traktor_kontrol_s4_logger] [info]   evdev_process_event                       count=51738834  min=     0µs  avg=     0µs  max=    71µs  total=  13558784µs
[2026-06-16 16:49:04.681] [traktor_kontrol_s4_logger] [info]   alsa_snd_ctl_elem_write                   count=      41  min=     6µs  avg=    60µs  max=   118µs  total=      2462µs
[2026-06-16 16:49:04.681] [traktor_kontrol_s4_logger] [info] ============================
```

What these numbers tell you:

| Counter | Calls | Meaning |
|---|---|---|
| `evdev_process_event` | 51.7M | Total loop iterations (the busy-spin inner loop) |
| `evdev_next_event` | 51.7M | `libevdev_next_event` calls — **most return `-EAGAIN` (no data)** |
| `evdev_translate_and_enqueue` | 52.3K | Actual hardware events received and translated |
| `midi_sendMessage` | 17.2K | MIDI messages sent to Mixxx |
| `alsa_set_led_value` / `alsa_snd_ctl_elem_write` | 41 | ALSA LED writes from Mixxx feedback |

The ratio of `evdev_next_event` (51.7M) to `evdev_translate_and_enqueue`
(52.3K) confirms the **busy-spin problem**: ~1000 empty iterations for every
real event.  Translation and MIDI send are fast (avg 1µs); ALSA writes are
slower (avg 60µs) but infrequent.  This data directly motivates replacing the
spin loop with poll-driven draining (see `docs/architecture-performance-plan.md`,
§2).

When `-DENABLE_PERF_COUNTERS=OFF` (the default), instrumentation compiles to
zero overhead — all macros expand to `((void)0)`.

## Replay test harness

The `replay_evdev_trace` tool exercises the input-translation pipeline without
a physical controller, RtMidi, or ALSA. It replays synthetic evdev event traces
through the same `EvDevEvent` → `Button`/`Knob`/`Jog`/`Slider` code path and
captures the resulting MIDI messages and LED commands for inspection.

### Build

The harness is built alongside the main driver — no extra dependencies needed:

```
mkdir build && cd build
cmake ..
make replay_evdev_trace
```

### Trace format

One event per line, comments start with `#`:

```
<type> <code> <value>
```

| Field | Meaning | Example |
|---|---|---|
| `type`  | `EV_KEY` (1) or `EV_ABS` (3) | `1` for button |
| `code`  | evdev event code (see `Button.cpp` / `config.json`) | `261` = CH1_CUE |
| `value` | Press (1) / Release (0) or absolute position | `1` = pressed |

Built-in test traces are in `tools/replay_evdev_trace/test_traces/`:

| Trace | Events tested |
|---|---|
| `headphone_press.txt` | CH1_EARPHONES press/release → `0xb0 0x44 0x01 / 0x00` |
| `cue_press.txt`      | CH1_CUE press/release → `0xb0 0x09 0x01 / 0x00` |
| `jog_rotation.txt`   | Sustained jog wheel turn → `0xb0 0x02 …` |
| `jog_touch.txt`      | Touch press/hold/release → `0xb0 0x03 …` |
| `deck_toggle_with_jog.txt` | Deck toggle interleaved with jog events |

### Run a trace

```
./replay_evdev_trace --config ../config.json --trace-file \
    ../tools/replay_evdev_trace/test_traces/cue_press.txt
```

Expected output:

```
Replaying 2 events...

=== Results ===
Events processed: 2
MIDI messages captured: 2
  First message:  [ 0xb0 0x09 0x01 ]
  Last message:  [ 0xb0 0x09 0x00 ]
LED commands captured: 0
```

### Run all traces at once

```
make run-replay-traces
```

### Options

| Flag | Purpose |
|---|---|
| `--config <path>` | Path to `config.json` |
| `--trace-file <path>` | Read trace from file (instead of stdin) |
| `--dump-midi` | Show every captured MIDI message |
| `--dump-leds` | Show every captured LED command |
| `--event-delay-ms <n>` | Sleep `n` ms between events (required for jog timing accumulator) |
| `--no-report` | Suppress perf-counter summary |
| `--help` | Show usage |

### Known limitations

- **Jog timing:** The jog wheel accumulator (`Jog::get_value_jog`) depends on
  wall-clock time. Without `--event-delay-ms`, all events arrive within
  microseconds and the accumulator never fires. Use `--event-delay-ms 10` for
  realistic replay.
- **Deck-toggle LEDs:** The toggle LED bulk writes are part of
  `EvDevHelper::read_events_from_device`, not the translation path. The
  harness captures MIDI translation correctly but does not replicate the
  toggle LED logic (the `AlsaHelper` stubs are ready to receive those calls).
- **VU meter / beat loop display:** These are driven by Mixxx MIDI feedback
  (`MidiHelper::midi_in_callback`) and are not exercised by the current traces.

## Installation
Installation process is pending, currently you only can download or clone the repo and compile with CMake (>3.16)

## IN PROCESS
