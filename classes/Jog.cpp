#include "Jog.h"
#include "MidiHelper.h"

map<int, Jog *> Jog::jog_mapping = {
        {52, new Jog(52, "JOG WHEEL CH1 / CH3", 0)},
        {53, new Jog(53, "JOG WHEEL CH2 / CH4", 0)},
        {26, new Jog(26, "JOG WHEEL CH1 / CH3 PRESSED", 0, 52)},
        {27, new Jog(27, "JOG WHEEL CH2 / CH4 PRESSED", 0, 53)}
};

Jog::Jog(int in_code, string in_name, int in_value, int in_paired_rot_code){
  code = in_code;
  name = in_name;
  value = in_value;
  sensitivity = 5;
  prev_control_value = -1000;
  counter = 0;
  updated = -1;
  was_touching = false;
  paired_rot_code = in_paired_rot_code;
}

void Jog::reset_accumulator(){
  prev_control_value = -1000;
  counter = 0;
  updated = -1;
}

int Jog::handle_event(RtMidiOut *midi_out, bool shift_ch1, bool shift_ch2, bool toggle_ac, bool toggle_bd, ConfigHelper *config_helper){
  shared_ptr<spdlog::logger> logger = spdlog::get(config_helper->get_string_value("traktor_s4_logger_name"));
  auto mm_it = MidiEventOut::midi_mapping.find(code);
  if (mm_it != MidiEventOut::midi_mapping.end()) {
    MidiEventOut *midi_event = mm_it->second;

    int midi_value = 0;

    if (midi_event->controller_type == "JOG_TOUCH"){
      bool now_touching = (value >= 3050);
      if (now_touching == was_touching) {
        return 0;
      }
      was_touching = now_touching;
      midi_value = now_touching ? 0x7f : 0x00;

      if (!now_touching && paired_rot_code != -1) {
        auto rot_it = jog_mapping.find(paired_rot_code);
        if (rot_it != jog_mapping.end()) {
          rot_it->second->reset_accumulator();
        }
      }
    }
    else{
      sensitivity = config_helper->get_int_value("jog_wheel_sensitivity");
      midi_value = get_value_jog();
    }

    if (midi_value == -1000){
      return 0;
    }

    auto message = UtilsHelper::create_message(shift_ch1, shift_ch2, toggle_ac, toggle_bd, midi_event, (unsigned char)midi_value);

    MidiHelper::enqueue_message(std::move(message));
  }

  return 0;
}

int Jog::get_value_jog(){
  if (prev_control_value == -1000){
    prev_control_value = value;
    updated = MidiEventOut::get_time();
    return -1000;
  }

  if (updated == -1){
    updated = MidiEventOut::get_time();
  }

  int diff = -1;

  if (value <= 255 && prev_control_value >= 767){
    diff = 1024 - prev_control_value + value;
  }
  else if (value >= 767 && prev_control_value <= 255){
    diff = value - 1024 - prev_control_value;
  }
  else{
    diff = value - prev_control_value;
  }

  prev_control_value = value;

  int64_t now = MidiEventOut::get_time();
  int64_t elapsed = now - updated;

  if (elapsed < sensitivity){
    counter += diff;
    return -1000;
  }
  else{
    if (elapsed > 50) {
      counter = 0;
      prev_control_value = value;
    }
    int midi_value = counter + diff;

    counter = 0;
    updated = now;

    if ((midi_value >= -64) && (midi_value < 0)){
      midi_value = 128 + midi_value;
    }
    else if(midi_value < -64){
      midi_value = 64;
    }
    else if (midi_value >= 63){
      midi_value = 63;
    }

    return midi_value;
  }
}
