#ifndef TRAKTOR_KONTROL_S4_MK1_DRIVER_LINUX_JOG_H
#define TRAKTOR_KONTROL_S4_MK1_DRIVER_LINUX_JOG_H

// --------------------------
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <map>
// --------------------------
#include "MidiEventOut.h"
#include "MidiHelper.h"
#include "UtilsHelper.h"
#include "ConfigHelper.h"

using namespace std;

class Jog
{
 public:
    Jog(int code,
        string name,
        int value,
        int paired_rot_code = -1);
    int code;
    string name;
    int value;
    int counter = 0;
    int prev_control_value;
    int sensitivity;
    int64_t updated;
    bool was_touching = false;
    int paired_rot_code;
    int handle_event(RtMidiOut *midi_out_port,
                     bool shift_ch1,
                     bool shift_ch2,
                     bool toggle_ac,
                     bool toggle_bd,
                     ConfigHelper *config);
    static map<int, Jog *> jog_mapping;
    int get_value_jog();
    void reset_accumulator();
};

#endif //TRAKTOR_KONTROL_S4_MK1_DRIVER_LINUX_JOG_H
