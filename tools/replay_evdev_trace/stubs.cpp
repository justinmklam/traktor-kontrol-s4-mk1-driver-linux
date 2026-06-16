// =============================================================================
// Stub implementations for replay-mode harness.
// Replaces the real MidiHelper and AlsaHelper with capture-collecting stubs
// so the translation pipeline can be exercised without real hardware.
// =============================================================================
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <string>
#include <iostream>
#include "AlsaHelper.h"
#include "MidiHelper.h"
#include "ConfigHelper.h"
#include "Led.h"

// ---------------------------------------------------------------------------
// Captured outputs — populated by the stubs, inspected by the harness.
// ---------------------------------------------------------------------------
std::vector<std::vector<unsigned char>> g_captured_midi;
std::vector<std::tuple<int, int, int>>      g_captured_leds;  // (control_id, value, card_id)

std::mutex g_capture_mutex;

// ---------------------------------------------------------------------------
// MidiHelper static members — only the bare minimum needed for linking.
// ---------------------------------------------------------------------------
std::queue<std::vector<unsigned char>> MidiHelper::s_midi_out_queue;
std::mutex                             MidiHelper::s_midi_out_queue_mutex;
std::condition_variable                MidiHelper::s_midi_out_queue_cv;
std::thread                            MidiHelper::s_midi_out_thread;
std::atomic<bool>                      MidiHelper::s_midi_out_running{false};
RtMidiOut                             *MidiHelper::s_pMidiOut = nullptr;

MidiHelper::MidiHelper(ConfigHelper *config) {
    config_helper = config;
    traktor_device_id = 0;
    pMidiOut = nullptr;
    pMidiIn  = nullptr;
}

MidiHelper::~MidiHelper() {
    // Nothing to clean up in replay mode.
}

void MidiHelper::midi_out_sender_loop() {
    // Not used in replay mode.
}

bool MidiHelper::close_input_port() const  { return true; }
bool MidiHelper::close_output_port() const { return true; }

void MidiHelper::show_midi_information(MidiHelper *, ConfigHelper *) {
    // Not needed in replay mode.
}

// The one function the translation path actually calls — capture instead of send.
void MidiHelper::enqueue_message(std::vector<unsigned char> message) {
    std::lock_guard<std::mutex> lock(g_capture_mutex);
    g_captured_midi.push_back(std::move(message));
}

// ---------------------------------------------------------------------------
// AlsaHelper stubs — capture LED commands.
// ---------------------------------------------------------------------------
snd_ctl_t *AlsaHelper::s_ctl   = nullptr;
int        AlsaHelper::s_card_id = -1;
std::mutex AlsaHelper::s_ctl_mutex;

void AlsaHelper::init_ctl(int, ConfigHelper*)          {}
void AlsaHelper::close_ctl(ConfigHelper*)              {}

int AlsaHelper::set_led_value(int card_id, int control_id, int led_value, ConfigHelper*) {
    std::lock_guard<std::mutex> lock(g_capture_mutex);
    g_captured_leds.emplace_back(control_id, led_value, card_id);
    return 0;
}

int AlsaHelper::bulk_led_value(int card_id, int control_ids[], int led_value, int num_controls, ConfigHelper* config) {
    for (int i = 0; i < num_controls; i++) {
        if (control_ids[i] != 0)
            set_led_value(card_id, control_ids[i], led_value, config);
    }
    return 0;
}

int AlsaHelper::get_traktor_device(ConfigHelper*) {
    return 0;  // fake card id
}
