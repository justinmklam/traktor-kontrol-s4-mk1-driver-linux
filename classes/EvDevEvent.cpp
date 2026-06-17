#include "EvDevEvent.h"
#include <linux/input-event-codes.h>

using namespace std;

Knob *knob_dev;
Slider *slider_dev;
Jog *jog_dev;
Button *button_dev;

EvDevEvent::EvDevEvent(__u16 in_type, __u16 in_code, __s32 in_value, timeval in_time) {
    type = in_type;
    code = in_code;
    value = in_value;
    time = in_time;
}

void EvDevEvent::handle_with(RtMidiOut *midi_out, int controller_id, bool shift_ch1, bool shift_ch2, bool toggle_ac, bool toggle_bd, ConfigHelper *config_helper){
  shared_ptr<spdlog::logger> logger = spdlog::get(config_helper->get_string_value("traktor_s4_logger_name"));
  logger->debug("[EvDevEvent::handle_with] Checking event to handle with...");

  // §7b: Integer switch instead of string map lookup
  switch (type) {
    case EV_KEY: { // 1
        logger->debug("[EvDevEvent::handle_with] Type: EV_KEY Code: {0}", code);
        auto it = Button::buttons_mapping.find(code);
        if (it == Button::buttons_mapping.end()){
          logger->debug("[EvDevEvent::handle_with] BUTTON not recognized with code: {0}", code);
          return;
        }
        button_dev = it->second;
        if (button_dev == nullptr){
          logger->debug("[EvDevEvent::handle_with] BUTTON not recognized with code: {0}", code);
          return;
        }

        button_dev->value = value;
        logger->debug("[EvDevEvent::handle_with] Get BUTTON. Code: {0}, Name: {1}, LED Code: {2}, Channel: {3}", to_string(button_dev->code), button_dev->name, to_string(button_dev->led_code), to_string(button_dev->channel));
        int status = button_dev->handle_event(midi_out, controller_id, shift_ch1, shift_ch2, toggle_ac, toggle_bd, config_helper);
        if (status < 0){
          logger->error("[EvDevEvent::handle_with] Error handling BUTTON with Code: {0} Name: {1} Status: {2}", to_string(button_dev->code), button_dev->name,  status);
        }
        break;
    }
    case EV_ABS: { // 3
        // §7c: Single map lookup for each type
        auto sl_it = Slider::sliders_mapping.find(code);
        if (sl_it != Slider::sliders_mapping.end()){
            slider_dev = sl_it->second;
            if (slider_dev == nullptr){
              logger->debug("[EvDevEvent::handle_with] SLIDER not recognized with code: {0}", code);
              return;
            }

            slider_dev->value = floor(value / 32);
            logger->debug("[EvDevEvent::handle_with] Get SLIDER. Code: {0}, Name: {1}, Value: {2}", to_string(slider_dev->code), slider_dev->name, to_string(slider_dev->value));
            int status = slider_dev->handle_event(midi_out, shift_ch1, shift_ch2, toggle_ac, toggle_bd, config_helper);
            if (status < 0){
              logger->debug("[EvDevEvent::handle_with] Error handling SLIDER with Code: {0} Name: {1} Status: {2}", to_string(slider_dev->code), slider_dev->name,  strerror(status));
            }
            break;
        }
        auto kn_it = Knob::knob_mapping.find(code);
        if (kn_it != Knob::knob_mapping.end()){
            knob_dev = kn_it->second;
            if (knob_dev == nullptr){
              logger->debug("[EvDevEvent::handle_with] KNOB not recognized with code: {0}", code);
              return;
            }

            knob_dev->value = value;
            logger->debug("[EvDevEvent::handle_with] Get KNOB. Code: {0}, Name: {1}, Value: {2}", to_string(knob_dev->code), knob_dev->name, to_string(knob_dev->value));
            int status = knob_dev->handle_event(midi_out, shift_ch1, shift_ch2, toggle_ac, toggle_bd, config_helper);
            if (status < 0){
              logger->debug("[EvDevEvent::handle_with] Error handling KNOB with Code: {0} Name: {1} Status: {2}", to_string(knob_dev->code), knob_dev->name,  strerror(status));
            }
            break;
        }
        auto jg_it = Jog::jog_mapping.find(code);
        if (jg_it != Jog::jog_mapping.end()){
            jog_dev = jg_it->second;
            if (jog_dev == nullptr){
              logger->debug("[EvDevEvent::handle_with] JOG WHEEL not recognized with code: {0}", code);
              return;
            }

            jog_dev->value = value;
            logger->debug("[EvDevEvent::handle_with] Get JOG HWEEL. Code: {0}, Name: {1}, Value: {2}", to_string(jog_dev->code), jog_dev->name, to_string(jog_dev->value));
            int status = jog_dev->handle_event(midi_out, shift_ch1, shift_ch2, toggle_ac, toggle_bd, config_helper);
            if (status < 0){
              logger->debug("[EvDevEvent::handle_with] Error handling JOG WHEEL with Code: {0} Name: {1} Status: {2}", to_string(button_dev->code), button_dev->name,  strerror(status));
            }
            break;
        }
        logger->debug("[EvDevEvent::handle_with] EV_ABS Event not handled: {0} {1} {2} {3}", to_string(type), to_string(code), to_string(value), to_string(time.tv_sec));
        break;
    }
    default:
        logger->debug("[EvDevEvent::handle_with] Event not recognized: Type: {0} Code: {1} Value: {2} Time: {3}", to_string(type), to_string(code), to_string(value), to_string(time.tv_sec));
        break;
    }
    logger->debug("[EvDevEvent::handle_with] Finished");
}
