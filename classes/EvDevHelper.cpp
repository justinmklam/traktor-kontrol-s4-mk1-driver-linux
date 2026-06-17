#include <iostream>
#include <pthread.h>
#include <sched.h>
#include "EvDevHelper.h"
#include "PerfCounters.h"

using namespace std;

// Set by signal handler in main.cpp — checked in the evdev poll loop
extern std::atomic<bool> g_shutdown_requested;

int traktor_device_id_ = 0;
atomic<bool> shift_ch1{false};
atomic<bool> shift_ch2{false};
atomic<bool> toggle_ac{false};
atomic<bool> toggle_bd{false};

EvDevHelper::EvDevHelper(ConfigHelper *config, int traktor_device_id, LedWriter *led_writer){
  config_helper = config;
  led_writer_ = led_writer;
  shared_ptr<spdlog::logger> logger = spdlog::get(config_helper->get_string_value("traktor_s4_logger_name"));
  traktor_device_id_ = traktor_device_id;
  if (traktor_device_id_ == -1){
    logger->error("[EvDevHelper:EvDevHelper] Device not found.... Bye!");
    exit(EXIT_FAILURE);
  }
  // Keep AlsaHelper::init_ctl for the sync init/shutdown path
  AlsaHelper::init_ctl(traktor_device_id_, config_helper);

  // Open evdev device once and store as member
  if (!open_evdev_device()) {
    logger->error("[EvDevHelper:EvDevHelper] Failed to open evdev controller device");
    exit(EXIT_FAILURE);
  }
  logger->debug("[EvDevHelper:EvDevHelper] evdev device opened successfully");
}

vector<string> EvDevHelper::get_evdev_device(){
    shared_ptr<spdlog::logger> logger = spdlog::get(config_helper->get_string_value("traktor_s4_logger_name"));
    string path = config_helper->get_string_value("evdev_helper_input_uri");
    vector<string> dev_input_files;
    logger->debug("[EvDevHelper::get_evdev_device] Retrieving evdev Devices...");
    for (const auto & entry : filesystem::directory_iterator(path)){
        string uri = entry.path();
        if (uri.find("event") != string::npos)
            dev_input_files.push_back(uri);
    }
    logger->debug("[EvDevHelper::get_evdev_device] Finished");
    return dev_input_files;
}

tuple<int, struct libevdev *> EvDevHelper::get_traktor_controller_device(){
    shared_ptr<spdlog::logger> logger = spdlog::get(config_helper->get_string_value("traktor_s4_logger_name"));
    logger->debug("[EvDevHelper::get_traktor_controller_device] Retrieving evdev Devices...");
    vector<string> uris = get_evdev_device();
    struct libevdev *dev = nullptr;
    for (const string & file : uris){
      logger->debug("[EvDevHelper::get_traktor_controller_device] Trying to open {0} file...", file);
        const int fd = open(file.c_str(), O_RDONLY | O_NONBLOCK);
        logger->debug("[EvDevHelper::get_traktor_controller_device] FD obtained: {0}", fd);
        try{
            int evdev = libevdev_new_from_fd(fd, &dev);
            if (evdev < 0) {
              logger->error("[EvDevHelper::get_traktor_controller_device] Failed to init libevdev ({0})", strerror(-evdev));
                close(fd);
                continue;
            }
            if ((libevdev_get_id_vendor(dev) == 0x17cc) && (libevdev_get_id_product(dev) == 0xbaff)){
              logger->debug("[EvDevHelper::get_traktor_controller_device] Found {1} Device: {0}", libevdev_get_name(dev), config_helper->get_string_value("alsa_device_name"));
                return make_tuple(evdev, dev);
            }
            // Not the Traktor device — clean up and continue
            libevdev_free(dev);
            dev = nullptr;
            close(fd);
        }
        catch (const evdevw::Exception &e) {
          logger->error("[EvDevHelper::get_traktor_controller_device] Error Reading: {0} Error: {1}", file, strerror(e.get_error()));
          close(fd);
        }
    }
    logger->debug("[EvDevHelper::get_traktor_controller_device] Finished");
    return make_tuple(-1, dev);
}

bool EvDevHelper::open_evdev_device() {
    auto [rc, dev] = get_traktor_controller_device();
    if (rc < 0 || dev == nullptr) {
        return false;
    }
    dev_ = dev;
    fd_ = libevdev_get_fd(dev_);
    return true;
}

void EvDevHelper::read_events_from_device(RtMidiOut *pMidiOut) {
    if (dev_ == nullptr || fd_ < 0) {
        shared_ptr<spdlog::logger> err_logger = spdlog::get(config_helper->get_string_value("traktor_s4_logger_name"));
        err_logger->error("[EvDevHelper::read_events_from_device] No evdev device available");
        return;
    }

    shared_ptr<spdlog::logger> logger = spdlog::get(config_helper->get_string_value("traktor_s4_logger_name"));
    logger->debug("[EvDevHelper::read_events_from_device] Reading events from {0}", config_helper->get_string_value("alsa_device_name"));

    logger->debug("Input device name: {0}", libevdev_get_name(dev_));
    logger->debug("Input device ID: bus {0} vendor {1} product {2}",
           libevdev_get_id_bustype(dev_),
           libevdev_get_id_vendor(dev_),
           libevdev_get_id_product(dev_));

    // §16: Elevate evdev reader thread priority for lower latency
    {
        struct sched_param param{};
        param.sched_priority = 20;
        if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) {
            // Non-fatal — may need CAP_SYS_NICE or root
            if (nice(-10) == -1 && errno != 0) {
                logger->debug("[EvDevHelper] Failed to set RT or nice priority: {0}", strerror(errno));
            }
        } else {
            logger->debug("[EvDevHelper] Set SCHED_FIFO priority 20 for evdev reader thread");
        }
    }

    // Cache config values used in the hot path (see §7a)
    const int shift_ch1_code = config_helper->get_int_value("alsa_device_shift_ch1_value");
    const int shift_ch2_code = config_helper->get_int_value("alsa_device_shift_ch2_value");
    const int toggle_ac_code  = config_helper->get_int_value("alsa_device_toggle_ac_value");
    const int toggle_bd_code  = config_helper->get_int_value("alsa_device_toggle_bd_value");

    struct pollfd pfd = {};
    pfd.fd = fd_;
    pfd.events = POLLIN;

    running_.store(true, memory_order_relaxed);

    while (running_.load(memory_order_relaxed) && !g_shutdown_requested.load(memory_order_relaxed)) {
        PERF_SCOPE("evdev_poll_wait");

        // Block until input is available
        int ret = poll(&pfd, 1, -1);
        if (ret < 0) {
            if (errno == EINTR) {
                // Interrupted by signal — re-check running flag
                continue;
            }
            logger->error("[EvDevHelper::read_events_from_device] poll() error: {0}", strerror(errno));
            break;
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            logger->error("[EvDevHelper::read_events_from_device] Device disconnected or error on fd {0}", fd_);
            break;
        }

        // Drain all available events
        int rc;
        do {
            PERF_SCOPE("evdev_process_event");
            struct input_event ev{};
            auto t_read = PerfCounters::now();
            rc = libevdev_next_event(dev_, LIBEVDEV_READ_FLAG_NORMAL, &ev);
            PerfCounters::record("evdev_next_event", t_read);

            if (rc == LIBEVDEV_READ_STATUS_SYNC) {
                // SYN_DROPPED occurred: drain sync events and discard them
                while (rc == LIBEVDEV_READ_STATUS_SYNC) {
                    rc = libevdev_next_event(dev_, LIBEVDEV_READ_FLAG_SYNC, &ev);
                }
                // After sync drain, re-enter normal read
                continue;
            }

            if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
                // §12: Skip EV_SYN events early — they carry no actionable data
                if (ev.type == EV_SYN) continue;

                if (ev.code == shift_ch1_code){ // SHIFT CH1 — momentary (§5)
                  if (ev.value == 1) shift_ch1.store(true, memory_order_relaxed);
                  else if (ev.value == 0) shift_ch1.store(false, memory_order_relaxed);
                  // ev.value == 2 is key repeat — ignore
                  continue;
                }
                if (ev.code == shift_ch2_code){ // SHIFT CH2 — momentary (§5)
                  if (ev.value == 1) shift_ch2.store(true, memory_order_relaxed);
                  else if (ev.value == 0) shift_ch2.store(false, memory_order_relaxed);
                  continue;
                }
                if ((ev.code == toggle_ac_code) && (ev.value == 1)){ // TOGGLE CH1 / CH3
                  auto t_toggle = PerfCounters::now();
                  toggle_ac = !toggle_ac;
                  if (toggle_ac){
                    led_writer_->set_leds({
                      {75, Led::ON}, {87, Led::ON}, {40, Led::ON}, {41, Led::ON}, {49, Led::ON},
                      {23, Led::OFF}, {14, Led::OFF}, {15, Led::OFF}, {86, Led::OFF}
                    });
                  }
                  else{
                    led_writer_->set_leds({
                      {86, Led::ON}, {23, Led::ON}, {14, Led::ON}, {15, Led::ON},
                      {75, Led::MIDDLE},
                      {87, Led::OFF}, {40, Led::OFF}, {41, Led::OFF}, {49, Led::OFF}
                    });
                  }
                  PerfCounters::record("toggle_ac_leds", t_toggle);
                  logger->debug("[EvDevHelper::read_events_from_device] Deck toggle AC Changed: {0}", toggle_ac);
                  continue;
                }
                if ((ev.code == toggle_bd_code) && (ev.value == 1)){ // TOGGLE CH2 / CH4
                  auto t_toggle = PerfCounters::now();
                  toggle_bd = !toggle_bd;
                  if (toggle_bd){
                    led_writer_->set_leds({
                      {53, Led::ON}, {54, Led::ON}, {62, Led::ON}, {131, Led::ON}, {119, Led::ON},
                      {130, Led::OFF}, {36, Led::OFF}, {27, Led::OFF}, {28, Led::OFF}
                    });
                  }
                  else{
                    led_writer_->set_leds({
                      {27, Led::ON}, {28, Led::ON}, {36, Led::ON}, {130, Led::ON},
                      {119, Led::MIDDLE},
                      {131, Led::OFF}, {62, Led::OFF}, {53, Led::OFF}, {54, Led::OFF}
                    });
                  }
                  PerfCounters::record("toggle_bd_leds", t_toggle);
                  logger->debug("[EvDevHelper::read_events_from_device] Deck toggle BD Changed: {0}", toggle_bd);
                  continue;
                }

#ifndef NDEBUG
                const char * type_name = libevdev_event_type_get_name(ev.type);
                const char * code_name = libevdev_event_code_get_name(ev.type, ev.code);

                if ((type_name != nullptr) && (code_name != nullptr)){
                  logger->debug("[EvDevHelper::read_events_from_device] Event: TypeName: {0} - CodeName: {1} - Type: {2} - Code: {3} - Value: {4} - Time: {5}",
                                type_name,
                                code_name,
                                ev.type,
                                ev.code,
                                ev.value,
                                ev.time.tv_sec);
                }
                else{
                  logger->debug("[EvDevHelper::read_events_from_device] Event: Type: {0} - Code: {1} - Value: {2} - Time: {3}",
                                ev.type,
                                ev.code,
                                ev.value,
                                ev.time.tv_sec);
                }
#endif

                auto t_translate = PerfCounters::now();
                auto *evdev_event = new EvDevEvent(ev.type, ev.code, ev.value, ev.time);
                evdev_event->handle_with(pMidiOut, traktor_device_id_, shift_ch1, shift_ch2, toggle_ac, toggle_bd, config_helper);
                PerfCounters::record("evdev_translate_and_enqueue", t_translate);
            }
        } while (rc == LIBEVDEV_READ_STATUS_SUCCESS);
    }
}

void EvDevHelper::initialize_buttons_leds(ConfigHelper *config_helper){
    shared_ptr<spdlog::logger> logger = spdlog::get(config_helper->get_string_value("traktor_s4_logger_name"));
    logger->debug("[EvDevHelper::initialize_buttons_leds] Initializing controller Leds....");
    int control_ids[Led::leds_mapping.size()];

    logger->debug("[EvDevHelper::initialize_buttons_leds] Using device {0}", to_string(traktor_device_id_));
    if (traktor_device_id_ != -1){
        map<int, Led *>::iterator it;
        int cont = 0;
        for (it = Led::leds_mapping.begin(); it != Led::leds_mapping.end(); it++)
        {
            if (it->second->by_default) {
              logger->debug("[EvDevHelper::initialize_buttons_leds] Preparing for init Led with Code {0}", to_string(it->second->code));
                control_ids[cont] = it->second->code;
                if (((cont % 5) == 0) && (cont > 0)){
                    AlsaHelper::bulk_led_value(traktor_device_id_, control_ids, Led::MIDDLE, cont + 1, config_helper);
                    cont = 0;
                }
                else
                    cont++;
            }
        }
        AlsaHelper::bulk_led_value(traktor_device_id_, control_ids, Led::MIDDLE, cont + 1, config_helper);
    }
    else{
      logger->error("[EvDevHelper::initialize_buttons_leds] Cannot use {0} device", to_string(traktor_device_id_));
        exit(EXIT_FAILURE);
    }
    logger->debug("[EvDevHelper::initialize_buttons_leds] Finished");
}

void EvDevHelper::shutdown_buttons_leds(ConfigHelper *config_helper){
    shared_ptr<spdlog::logger> logger = spdlog::get(config_helper->get_string_value("traktor_s4_logger_name"));
    logger->debug("[EvDevHelper::shutdown_buttons_leds] Shutting down controller Leds....");
    map<int, Led *>::iterator it;
    int control_ids[Led::leds_mapping.size()];
    int cont = 0;
    for (it = Led::leds_mapping.begin(); it != Led::leds_mapping.end(); it++)
    {
      control_ids[cont] = it->second->code;
      logger->debug("Preparing for shutdown Led Code {0}", to_string(it->second->code));
      if (((cont % 5) == 0) && (cont > 0)){
          AlsaHelper::bulk_led_value(traktor_device_id_, control_ids, Led::OFF, cont + 1, config_helper);
          cont = 0;
      }
      else
          cont++;
    }
    AlsaHelper::bulk_led_value(traktor_device_id_, control_ids, Led::OFF, cont + 1, config_helper);
    logger->debug("[EvDevHelper::shutdown_buttons_leds] Finished");
}
