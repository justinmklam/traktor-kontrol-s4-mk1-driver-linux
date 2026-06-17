#ifndef TRAKTOR_KONTROL_S4_MK1_DRIVER_LINUX_EVDEVHELPER_H
#define TRAKTOR_KONTROL_S4_MK1_DRIVER_LINUX_EVDEVHELPER_H

// --------------------------
#include <cstring>
#include <unistd.h>
#include <stdlib.h>
#include <filesystem>
#include <vector>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <iostream>
#include <typeinfo>
#include <variant>
#include <libevdev/libevdev.h>
#include <map>
#include <tuple>
#include <signal.h>
#include <poll.h>
#include <atomic>
// --------------------------
#include "spdlog/spdlog.h"
#include "evdevw.hpp"
#include "Button.h"
#include "Knob.h"
#include "Led.h"
#include "Knob.h"
#include "EvDevEvent.h"
#include "AlsaHelper.h"
#include "UtilsHelper.h"
#include "ConfigHelper.h"
#include "LedWriter.h"

using namespace std;

class EvDevHelper
{
 private:
    vector<string> get_evdev_device();
    ConfigHelper *config_helper;
    LedWriter *led_writer_ = nullptr;
    struct libevdev *dev_ = nullptr;
    int fd_ = -1;
    atomic<bool> running_{true};
    bool open_evdev_device();

 public:
    EvDevHelper(ConfigHelper *config, int traktor_device_id, LedWriter *led_writer);
    tuple<int, struct libevdev *> get_traktor_controller_device();
    void read_events_from_device(RtMidiOut *midi_out_port);
    static void initialize_buttons_leds(ConfigHelper *config_helper);
    static void shutdown_buttons_leds(ConfigHelper *config_helper);
};

#endif //TRAKTOR_KONTROL_S4_MK1_DRIVER_LINUX_EVDEVHELPER_H
