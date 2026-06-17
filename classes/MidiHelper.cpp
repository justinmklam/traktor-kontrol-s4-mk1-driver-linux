#include "MidiHelper.h"
#include "PerfCounters.h"
#include "LedWriter.h"

using namespace std;

std::deque<std::vector<unsigned char>> MidiHelper::s_midi_out_queue;
std::mutex MidiHelper::s_midi_out_queue_mutex;
std::condition_variable MidiHelper::s_midi_out_queue_cv;
std::thread MidiHelper::s_midi_out_thread;
std::atomic<bool> MidiHelper::s_midi_out_running{false};
RtMidiOut *MidiHelper::s_pMidiOut = nullptr;

class callbackData{
 public:
  int traktor_device_id = 0;
  ConfigHelper *config{};
  LedWriter *led_writer = nullptr;
};

MidiHelper::MidiHelper(ConfigHelper *config, int traktor_dev_id, LedWriter *led_writer){
  shared_ptr<spdlog::logger> logger = spdlog::get(config->get_string_value("traktor_s4_logger_name"));
  config_helper = config;
  led_writer_ = led_writer;
  try {
      traktor_device_id = traktor_dev_id;
      if (traktor_device_id == -1){
        logger->error("[MidiHelper] Traktor Kontrol S4 Device not found.... Bye!");
        exit(EXIT_FAILURE);
      }
      pMidiIn = new RtMidiIn(RtMidi::UNSPECIFIED, config->get_string_value("midi_client_name"), config->get_int_value("midi_queue_size_limit"));
      pMidiIn->openVirtualPort(config->get_string_value("midi_virtual_port_name"));


      auto *data = new callbackData();
      data->traktor_device_id = traktor_device_id;
      data->config = config;
      data->led_writer = led_writer;

      pMidiIn->setErrorCallback(reinterpret_cast<RtMidiErrorCallback>(midi_in_error_callback), (void *) data);
      pMidiIn->setCallback(reinterpret_cast<RtMidiIn::RtMidiCallback>(midi_in_callback), (void *) data);

      pMidiOut = new RtMidiOut(RtMidi::UNSPECIFIED, config->get_string_value("midi_client_name"));
      pMidiOut->openVirtualPort(config->get_string_value("midi_virtual_port_name"));

      s_pMidiOut = pMidiOut;
      s_midi_out_running = true;
      s_midi_out_thread = std::thread(&MidiHelper::midi_out_sender_loop);
  }
  catch ( RtMidiError &error ) {
    logger->error("[RtMidiHelper::RtMidiHelper] RtMidi Error: {0}", error.getMessage());
      exit(EXIT_FAILURE);
  }
}

MidiHelper::~MidiHelper(){
  s_midi_out_running = false;
  s_midi_out_queue_cv.notify_all();
  if (s_midi_out_thread.joinable()){
    s_midi_out_thread.join();
  }
  if (pMidiOut){
    pMidiOut->closePort();
    delete pMidiOut;
  }
  if (pMidiIn){
    pMidiIn->closePort();
    delete pMidiIn;
  }
}

void MidiHelper::midi_out_sender_loop(){
  while (s_midi_out_running){
    std::vector<unsigned char> message;
    {
      std::unique_lock<std::mutex> lock(s_midi_out_queue_mutex);
      s_midi_out_queue_cv.wait(lock, []{ return !s_midi_out_queue.empty() || !s_midi_out_running; });
      if (!s_midi_out_running && s_midi_out_queue.empty()){
        return;
      }
      message = std::move(s_midi_out_queue.front());
      s_midi_out_queue.pop_front();
    }
    if (s_pMidiOut){
      try{
        auto t_send = PerfCounters::now();
        s_pMidiOut->sendMessage(&message);
        PerfCounters::record("midi_sendMessage", t_send);
      }
      catch (exception &e){
        auto logger = spdlog::get("traktor_kontrol_s4_logger");
        if (logger) logger->error("[MidiHelper::midi_out_sender_loop] Error sending MIDI: {0}", e.what());
      }
    }
  }
}

// §6/§8: Max queue depth to bound memory usage under jog bursts
static constexpr size_t MAX_QUEUE_DEPTH = 256;

void MidiHelper::enqueue_message(std::vector<unsigned char> message){
  {
    std::lock_guard<std::mutex> lock(s_midi_out_queue_mutex);

    // When over limit, drop the oldest jog-type message (status 0x02 or 0x20)
    // Never drop button messages.
    if (s_midi_out_queue.size() >= MAX_QUEUE_DEPTH) {
      for (auto it = s_midi_out_queue.begin(); it != s_midi_out_queue.end(); ++it) {
        if (it->size() >= 2 && ((*it)[1] == 0x02 || (*it)[1] == 0x20)) {
          s_midi_out_queue.erase(it);
          break;
        }
      }
    }

    s_midi_out_queue.push_back(std::move(message));
  }
  s_midi_out_queue_cv.notify_one();
}

[[maybe_unused]] bool MidiHelper::close_input_port() const
{
    pMidiIn->closePort();
    return true;
}

[[maybe_unused]] bool MidiHelper::close_output_port() const
{
    pMidiOut->closePort();
    return true;
}

RtMidiErrorCallback MidiHelper::midi_in_error_callback(RtMidiError::Type type, const string &error_message, void *userData){
  auto* data = reinterpret_cast<callbackData *>(userData);
  auto sharedFileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(data->config->get_string_value("midi_out_callback_error_log_file"));
  auto logger = std::make_shared<spdlog::logger>(data->config->get_string_value("midi_out_callback_error_logger_name"), sharedFileSink);
  logger->info("[MidiHelper::midi_in_error_callback] Error MIDI Error callback.... {0}", error_message);
  return nullptr;
}

RtMidiIn::RtMidiCallback MidiHelper::midi_in_callback(double deltatime, std::vector<unsigned char> *message, void *userData){
    PERF_SCOPE("midi_in_callback");
    auto* data = reinterpret_cast<callbackData *>(userData);
    unsigned char channel = message->at(0);
    unsigned char status = message->at(1);
    unsigned char value = message->at(2);
    shared_ptr<spdlog::logger> logger = spdlog::get(data->config->get_string_value("traktor_s4_logger_name"));

    logger->debug("[MidiHelper::midi_in_callback] MIDI In callback received with message: Channel: {0} Status: {1} Value: {2}", channel, status, value);

    try {
      auto it = MidiEventIn::midi_in_mapping.find((char)status);
      if ((it != MidiEventIn::midi_in_mapping.end()) && (channel >= 0xb0) && (channel <= 0xb4)){
        if ((status == 0x50) && (value >= 0x0) && (value <= 0xc)){
          if (!UtilsHelper::show_beat_loop_display(channel, status, value, data->traktor_device_id, data->config, data->led_writer))
          {
            logger->debug("[MidiHelper::midi_in_callback] Error processing beat loop size: Channel: {0} Status: {1} Value: {2}", channel, status, value);
            return nullptr;
          }
          logger->debug("[MidiHelper::midi_in_callback] MIDI In callback performed");
          return nullptr;
        }
        string control_id = it->second->check_channel_value(channel);

        if ((control_id != "-") && control_id.length() > 3){
          if (!UtilsHelper::show_vumeters_leds(value, data->traktor_device_id, control_id, data->config, data->led_writer)){
            logger->debug("[MidiHelper::midi_in_callback] Error processing vu meters: Channel: {0} Status: {1} Value: {2}", channel, status, value);
            return nullptr;
          }
          logger->debug("[MidiHelper::midi_in_callback] MIDI In callback performed");
          return nullptr;
        }
        else{
          if (control_id != "-"){
            if (!UtilsHelper::show_static_leds(value, data->traktor_device_id, control_id, data->config, data->led_writer)){
              logger->debug("[MidiHelper::midi_in_callback] Error processing static Led: Channel: {0} Status: {1} Value: {2}", channel, status, value);
              return nullptr;
            }
            logger->debug("[MidiHelper::midi_in_callback] MIDI In callback performed");
            return nullptr;
          }
        }
      }
    }
    catch (...){ }
  logger->debug("[MidiHelper::midi_in_callback] MIDI In callback performed");
  return nullptr;
}

void MidiHelper::show_midi_information(MidiHelper *midi_helper, ConfigHelper *config){
    shared_ptr<spdlog::logger> logger = spdlog::get(config->get_string_value("traktor_s4_logger_name"));
    unsigned int nPorts = midi_helper->pMidiIn->getPortCount();
    logger->debug("[RtMidiHelper::show_midi_information] There are {0} MIDI input sources available", nPorts);
    std::string portName;
    for ( unsigned int i=0; i < nPorts; i++ ) {
        try {
            portName = midi_helper->pMidiIn->getPortName(i);
        }
        catch ( RtMidiError &error ) {
          logger->error("[RtMidiHelper::show_midi_information] {0}", error.getMessage());
        }
        logger->debug("[RtMidiHelper::show_midi_information]    Input Port #{0}: {1}", i+1, portName);
    }

    nPorts = midi_helper->pMidiOut->getPortCount();
    logger->debug("[RtMidiHelper::show_midi_information] There are {0} MIDI output sources available", nPorts);
    for ( unsigned int i=0; i < nPorts; i++ ) {
        try {
            portName = midi_helper->pMidiOut->getPortName(i);
        }
        catch (RtMidiError &error) {
          logger->error("[RtMidiHelper::show_midi_information] {0}", error.getMessage());
        }
        logger->debug("[RtMidiHelper::show_midi_information]    Output Port #{0}: {1}", i+1, portName);
    }
}
