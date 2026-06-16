#ifndef TRAKTOR_KONTROL_S4_MK1_DRIVER_LINUX_RTMIDIHELPER_H
#define TRAKTOR_KONTROL_S4_MK1_DRIVER_LINUX_RTMIDIHELPER_H

// --------------------------
#include <iostream>
#include <cstdlib>
#include <string>
#ifndef REPLAY_MODE
#include <rtmidi/RtMidi.h>
#else
// Stub types needed by translation code when linking without RtMidi
struct RtMidiOut {};
struct RtMidiIn {
    using RtMidiCallback = void*;
};
using RtMidiErrorCallback = void*;
namespace RtMidi {
    enum Api { UNSPECIFIED };
}
class RtMidiError : public std::exception {
public:
    std::string getMessage() const { return "stub"; }
    enum Type { WARNING };
};
#endif
#include <sstream>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
// --------------------------
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "Button.h"
#include "Slider.h"
#include "Jog.h"
#include "Knob.h"
#include "Led.h"
#ifndef REPLAY_MODE
#include "EvDevHelper.h"
#endif
#include "UtilsHelper.h"
#include "MidiEventIn.h"
#include "ConfigHelper.h"

using namespace std;

class MidiHelper
{
 private:
    static RtMidiIn::RtMidiCallback midi_in_callback(double deltatime,
                                                     std::vector< unsigned char> *message,
                                                     void *user_data);
    static RtMidiErrorCallback midi_in_error_callback(RtMidiError::Type type,
                                                      const string &error_message,
                                                      void *user_data);
    ConfigHelper *config_helper;

    static std::queue<std::vector<unsigned char>> s_midi_out_queue;
    static std::mutex s_midi_out_queue_mutex;
    static std::condition_variable s_midi_out_queue_cv;
    static std::thread s_midi_out_thread;
    static std::atomic<bool> s_midi_out_running;
    static RtMidiOut *s_pMidiOut;
    static void midi_out_sender_loop();

 public:
    MidiHelper(ConfigHelper *config);
    ~MidiHelper();
    RtMidiOut *pMidiOut = 0;
    RtMidiIn *pMidiIn = 0;
    [[maybe_unused]] bool close_input_port() const;
    [[maybe_unused]] bool close_output_port() const;
    static void show_midi_information(MidiHelper *midi_helper, ConfigHelper *config);
    static void enqueue_message(std::vector<unsigned char> message);
    int traktor_device_id;
};

#endif //TRAKTOR_KONTROL_S4_MK1_DRIVER_LINUX_RTMIDIHELPER_H
