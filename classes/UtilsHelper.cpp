#include "UtilsHelper.h"
#include "LedWriter.h"

using namespace std;

vector<string> UtilsHelper::explode(string& string_to_explode,
                              const char& separator)
{
  string next;
  vector<string> result;
  for (char it : string_to_explode) {
    if (it == separator) {
      if (!next.empty()) {
        result.push_back(next);
        next.clear();
      }
    }
    else {
      next += it;
    }
  }
  if (!next.empty())
    result.push_back(next);
  return result;
}

std::vector<unsigned char> UtilsHelper::create_message(bool shift1, bool shift2, bool toggle_ac, bool toggle_bd, MidiEventOut *midi_event, unsigned char value)
{
  int channel, status;
  channel = status = -1;
  std::vector<unsigned char> message;

  if ((midi_event->channel_byte == 0xb0) || (midi_event->channel_byte == 0xb2)) {
    if (shift1 && !toggle_ac) {
      channel = midi_event->tgl_off_shf_on_channel_byte;
      status = midi_event->tgl_off_shf_on_status_byte;
    }
    if (!shift1 && toggle_ac) {
      channel = midi_event->tgl_on_shf_off_channel_byte;
      status = midi_event->tgl_on_shf_off_status_byte;
    }
    if (shift1 && toggle_ac) {
      channel = midi_event->tgl_on_shf_on_channel_byte;
      status = midi_event->tgl_on_shf_on_status_byte;
    }
  }
  else if ((midi_event->channel_byte == 0xb1) || (midi_event->channel_byte == 0xb3)){
    if (shift2 && !toggle_bd){
      channel = midi_event->tgl_off_shf_on_channel_byte;
      status = midi_event->tgl_off_shf_on_status_byte;
    }
    if (!shift2 && toggle_bd){
      channel = midi_event->tgl_on_shf_off_channel_byte;
      status = midi_event->tgl_on_shf_off_status_byte;
    }
    if (shift2 && toggle_bd){
      channel = midi_event->tgl_on_shf_on_channel_byte;
      status = midi_event->tgl_on_shf_on_status_byte;
    }
  }
  else if (midi_event->channel_byte == 0xb4){
    if (shift1){
      channel = midi_event->tgl_off_shf_on_channel_byte;
      status = midi_event->tgl_off_shf_on_status_byte;
    }
    if (shift2){
      channel = midi_event->tgl_on_shf_on_channel_byte;
      status = midi_event->tgl_on_shf_on_status_byte;
    }
  }

  if ((int)channel == -1){
    channel = midi_event->channel_byte;
  }

  if ((int)status == -1){
    status = midi_event->status_byte;
  }

  if ((midi_event->controller_type == "ROT_MOVE") && (midi_event->status_byte == 0x13) && (value == 0xff)){
    status = 0x14;
    value = 1;
  }
  message.push_back(channel);
  message.push_back(status);
  message.push_back(value);

  return message;
}

bool UtilsHelper::show_beat_loop_display(unsigned char channel, unsigned char status, unsigned char value, int traktor_device_id, ConfigHelper *config_helper, LedWriter *led_writer)
{
  shared_ptr<spdlog::logger> logger = spdlog::get(config_helper->get_string_value("traktor_s4_logger_name"));

  int beat_equivalences[12] = {32, 16, 8, 4, 2, 1, 2, 4, 8, 16, 32, 64};
  int loop_value = beat_equivalences[value];
  int units = (int)loop_value % 10;
  int tens = ((int)loop_value / 10) %10;
  int segments_to_show_units[Led::total_segments], segments_to_show_tens[Led::total_segments];
  logger->debug("[MidiHelper::midi_in_callback] Beat loop size received, Channel: {0} Status: {1} Value: {2} Loop Value: {3}", channel, status, value, loop_value);
  if ((channel == 0xb0) || (channel == 0xb2)){
    if (value <= 0x4){
      if (led_writer) led_writer->set_led(Led::ch1_digit1_led_dot, Led::ON);
    }
    else{
      if (led_writer) led_writer->set_led(Led::ch1_digit1_led_dot, Led::OFF);
    }
    for (int i = Led::ch1_digit1_led_numbers[0]; i <= Led::ch1_digit2_led_numbers[Led::total_segments - 1]; i++){
      if (i != Led::ch1_digit1_led_dot)
        if (led_writer) led_writer->set_led(i, Led::OFF);
    }
    for (int i = 0; i < Led::total_segments; i++){
      segments_to_show_units[i] = Led::numbers[units][i] * Led::ch1_digit2_led_numbers[i];
    }
    for (int i = 0; i < Led::total_segments; i++){
      segments_to_show_tens[i] = Led::numbers[tens][i] * Led::ch1_digit1_led_numbers[i];
    }
  }
  else if ((channel == 0xb1) || (channel == 0xb3)){
    if (value <= 0x4){
      if (led_writer) led_writer->set_led(Led::ch2_digit1_led_dot, Led::ON);
    }
    else{
      if (led_writer) led_writer->set_led(Led::ch2_digit1_led_dot, Led::OFF);
    }
    for (int i = Led::ch2_digit1_led_numbers[0]; i <= Led::ch2_digit2_led_numbers[Led::total_segments - 1]; i++){
      if (i != Led::ch2_digit1_led_dot)
        if (led_writer) led_writer->set_led(i, Led::OFF);
    }
    for (int i = 0; i < Led::total_segments; i++){
      segments_to_show_units[i] = Led::numbers[units][i] * Led::ch2_digit2_led_numbers[i];
    }
    for (int i = 0; i < Led::total_segments; i++){
      segments_to_show_tens[i] = Led::numbers[tens][i] * Led::ch2_digit1_led_numbers[i];
    }
  }

  // Enqueue all segment writes as batches
  if (led_writer) {
    for (int i = 0; i < Led::total_segments; i++) {
      if (segments_to_show_units[i] != 0)
        led_writer->set_led(segments_to_show_units[i], Led::ON);
      if (segments_to_show_tens[i] != 0)
        led_writer->set_led(segments_to_show_tens[i], Led::ON);
    }
  }
  return true;
}

bool UtilsHelper::show_vumeters_leds(unsigned char value, int traktor_device_id, string control_id, ConfigHelper *config_helper, LedWriter *led_writer)
{
  // §7f: Static cache to avoid re-splitting the same control_id strings on every callback
  static unordered_map<string, vector<string>> vu_cache;
  auto cache_it = vu_cache.find(control_id);
  if (cache_it == vu_cache.end()) {
    vector<string> parsed = UtilsHelper::explode(control_id, ' ');
    cache_it = vu_cache.emplace(std::move(control_id), std::move(parsed)).first;
  }
  const vector<string>& control_array = cache_it->second;
  int full_brightness = 0;
  if (value > 1){
    int light = value - 1;
    full_brightness = floor(light / 21);
    int partial = light % 21;
    for (int i = 0; i < full_brightness; i++){
      if (led_writer) led_writer->set_led(stoi(control_array[i]), Led::ON);
    }
    if (partial > 0){
      int alsa_values[20] = {1, 2, 4, 5, 7, 8, 10, 11, 13, 14, 16, 17, 19, 20, 22, 23, 25, 26, 28, 29};
      if (led_writer) led_writer->set_led(stoi(control_array[full_brightness]), alsa_values[partial - 1]);
    }
    // §15: Turn off segments above the current level
    int first_off = (partial > 0) ? full_brightness + 1 : full_brightness;
    for (int i = first_off; i < (int)control_array.size(); i++){
      if (led_writer) led_writer->set_led(stoi(control_array[i]), Led::OFF);
    }
  }
  else if (value == 1){
    if (led_writer) led_writer->set_led(stoi(control_array[0]), Led::MIDDLE);
  }
  else{
    if (led_writer) led_writer->set_led(stoi(control_array[0]), Led::OFF);
  }
  return true;
}

bool UtilsHelper::show_static_leds(unsigned char value, int traktor_device_id, string control_id, ConfigHelper *config_helper, LedWriter *led_writer)
{
  int control_id_num = stoi(control_id);
  if ((control_id_num >= 1) && (control_id_num <= 163)){
    if (value >= 1){
      if (led_writer) led_writer->set_led(control_id_num, Led::ON);
    }
    else{
      static const std::vector<int> to_off = {160, 161, 162, 155, 156, 157, 64, 65, 51, 52, 38, 39, 25, 26, 6, 7, 10, 24, 37, 50, 63, 67, 69, 71, 73, 111, 113, 115, 117};
      int target = std::find(to_off.begin(), to_off.end(), control_id_num) != to_off.end() ? Led::OFF : Led::MIDDLE;
      if (led_writer) led_writer->set_led(control_id_num, target);
    }
  }
  return true;
}