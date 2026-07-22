#include "EvDevHelper.h"

#include <poll.h>

#include <cerrno>
#include <chrono>
#include <iostream>
#include <thread>

using namespace std;

int traktor_device_id_ = 0;
bool shift_ch1 = false;
bool shift_ch2 = false;
bool toggle_ac = false;
bool toggle_bd = false;

EvDevHelper::EvDevHelper(ConfigHelper *config) {
  config_helper = config;
  shared_ptr<spdlog::logger> logger =
      spdlog::get(config_helper->get_string_value("traktor_s4_logger_name"));
  traktor_device_id_ = AlsaHelper::get_traktor_device(config_helper);
  if (traktor_device_id_ == -1) {
    logger->warn(
        "[EvDevHelper:EvDevHelper] {0} device not found; waiting "
        "for controller connection",
        config_helper->get_string_value("alsa_device_name"));
  }
}

vector<string> EvDevHelper::get_evdev_device() {
  shared_ptr<spdlog::logger> logger =
      spdlog::get(config_helper->get_string_value("traktor_s4_logger_name"));
  string path = config_helper->get_string_value("evdev_helper_input_uri");
  vector<string> dev_input_files;
  logger->debug("[EvDevHelper::get_evdev_device] Retrieving evdev Devices...");
  for (const auto &entry : filesystem::directory_iterator(path)) {
    string uri = entry.path();
    if (uri.find("event") != string::npos) dev_input_files.push_back(uri);
  }
  logger->debug("[EvDevHelper::get_evdev_device] Finished");
  return dev_input_files;
}

tuple<int, struct libevdev *> EvDevHelper::get_traktor_controller_device() {
  shared_ptr<spdlog::logger> logger =
      spdlog::get(config_helper->get_string_value("traktor_s4_logger_name"));
  logger->debug(
      "[EvDevHelper::get_traktor_controller_device] Retrieving evdev "
      "Devices...");
  vector<string> uris = get_evdev_device();
  for (const string &file : uris) {
    logger->debug(
        "[EvDevHelper::get_traktor_controller_device] Trying to open {0} "
        "file...",
        file);
    const int fd = open(file.c_str(), O_RDONLY | O_NONBLOCK);
    logger->debug(
        "[EvDevHelper::get_traktor_controller_device] FD obtained: {0}", fd);
    if (fd < 0) {
      logger->debug(
          "[EvDevHelper::get_traktor_controller_device] Failed to open {0}: "
          "{1}",
          file, strerror(errno));
      continue;
    }
    struct libevdev *dev = nullptr;
    try {
      int evdev = libevdev_new_from_fd(fd, &dev);
      if (evdev < 0) {
        logger->debug(
            "[EvDevHelper::get_traktor_controller_device] Failed to init "
            "libevdev ({0})",
            strerror(-evdev));
        close(fd);
        continue;
      }
      if ((libevdev_get_id_vendor(dev) == 0x17cc) &&
          (libevdev_get_id_product(dev) == 0xbaff)) {
        logger->debug(
            "[EvDevHelper::get_traktor_controller_device] Found {1} Device: "
            "{0}",
            libevdev_get_name(dev),
            config_helper->get_string_value("alsa_device_name"));
        return make_tuple(evdev, dev);
      }
      libevdev_free(dev);
      close(fd);
    } catch (const evdevw::Exception &e) {
      logger->error(
          "[EvDevHelper::get_traktor_controller_device] Error Reading: {0} "
          "Error: {1}",
          file, strerror(e.get_error()));
      if (dev != nullptr) {
        libevdev_free(dev);
      }
      close(fd);
    }
  }
  logger->debug("[EvDevHelper::get_traktor_controller_device] Finished");
  return make_tuple(-1, nullptr);
}

void EvDevHelper::check_evdev_status() {
  shared_ptr<spdlog::logger> logger =
      spdlog::get(config_helper->get_string_value("traktor_s4_logger_name"));
  const auto retry_delay = chrono::microseconds(config_helper->get_int_value(
      "evdev_helper_time_to_wait_in_microseconds"));

  while (true) {
    struct libevdev *dev = nullptr;
    int rc = 1;
    tie(rc, dev) = get_traktor_controller_device();
    traktor_device_id_ = AlsaHelper::get_traktor_device(config_helper);
    if (rc >= 0 && traktor_device_id_ != -1) {
      const int fd = libevdev_get_fd(dev);
      libevdev_free(dev);
      close(fd);
      logger->info("[EvDevHelper::check_evdev_status] Controller connected");
      return;
    }

    if (dev != nullptr) {
      const int fd = libevdev_get_fd(dev);
      libevdev_free(dev);
      close(fd);
    }
    logger->info(
        "[EvDevHelper::check_evdev_status] Controller unavailable; "
        "retrying connection");
    this_thread::sleep_for(retry_delay);
  }
}

void EvDevHelper::read_events_from_device(RtMidiOut *pMidiOut) {
  shared_ptr<spdlog::logger> logger =
      spdlog::get(config_helper->get_string_value("traktor_s4_logger_name"));
  logger->debug(
      "[EvDevHelper::read_events_from_device] Reading events from {0}",
      config_helper->get_string_value("alsa_device_name"));
  while (true) {
    struct libevdev *dev = nullptr;
    int rc = 1;
    tie(rc, dev) = get_traktor_controller_device();
    if (rc < 0) {
      logger->info(
          "[EvDevHelper::read_events_from_device] Controller "
          "disconnected; waiting for reconnection");
      this_thread::sleep_for(chrono::microseconds(config_helper->get_int_value(
          "evdev_helper_time_to_wait_in_microseconds")));
      continue;
    }

    traktor_device_id_ = AlsaHelper::get_traktor_device(config_helper);
    if (traktor_device_id_ == -1) {
      const int fd = libevdev_get_fd(dev);
      libevdev_free(dev);
      close(fd);
      logger->info(
          "[EvDevHelper::read_events_from_device] ALSA controller "
          "device unavailable; waiting for reconnection");
      this_thread::sleep_for(chrono::microseconds(config_helper->get_int_value(
          "evdev_helper_time_to_wait_in_microseconds")));
      continue;
    }

    const int fd = libevdev_get_fd(dev);
    logger->info("[EvDevHelper::read_events_from_device] Controller connected");
    initialize_buttons_leds(config_helper);
    struct pollfd pfd {};
    pfd.fd = fd;
    pfd.events = POLLIN;
    bool reconnect = false;

    while (!reconnect) {
      int poll_result = poll(&pfd, 1, -1);
      if (poll_result < 0) {
        if (errno == EINTR) {
          continue;
        }
        logger->error("[EvDevHelper::read_events_from_device] poll failed: {0}",
                      strerror(errno));
        reconnect = true;
        continue;
      }
      if ((pfd.revents & POLLERR) || (pfd.revents & POLLHUP) ||
          (pfd.revents & POLLNVAL)) {
        logger->error(
            "[EvDevHelper::read_events_from_device] evdev device disconnected "
            "or "
            "became invalid: {0}",
            pfd.revents);
        reconnect = true;
        continue;
      }
      if (!(pfd.revents & POLLIN)) {
        continue;
      }

      while (true) {
        struct input_event ev {};
        rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
        if (rc == -EAGAIN) {
          break;
        }
        if (rc == LIBEVDEV_READ_STATUS_SYNC) {
          logger->debug(
              "[EvDevHelper::read_events_from_device] evdev sync dropped; "
              "draining sync events");
          do {
            rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_SYNC, &ev);
          } while (rc == LIBEVDEV_READ_STATUS_SYNC);
          continue;
        }
        if (rc < 0) {
          logger->error(
              "[EvDevHelper::read_events_from_device] libevdev_next_event "
              "failed: {0}",
              strerror(-rc));
          reconnect = true;
          break;
        }
        if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
          if (ev.type == EV_SYN) {
            continue;
          }
          if (ev.code == config_helper->get_int_value(
                             "alsa_device_shift_ch1_value")) {  // SHIFT CH1
            shift_ch1 = !shift_ch1;
            continue;
          }
          if (ev.code == config_helper->get_int_value(
                             "alsa_device_shift_ch2_value")) {  // SHIFT CH2
            shift_ch2 = !shift_ch2;
            continue;
          }
          if ((ev.code ==
               config_helper->get_int_value("alsa_device_toggle_ac_value")) &&
              (ev.value == 1)) {  // TOGGLE CH1 / CH3
            toggle_ac = !toggle_ac;
            if (toggle_ac) {
              int to_on[] = {75, 87, 40, 41, 49};
              int to_off[] = {23, 14, 15, 86};

              AlsaHelper::bulk_led_value(traktor_device_id_, to_on, Led::ON, 5,
                                         config_helper);
              AlsaHelper::bulk_led_value(traktor_device_id_, to_off, Led::OFF,
                                         4, config_helper);
            } else {
              int to_on[] = {86, 23, 14, 15};
              int to_low[] = {75};
              int to_off[] = {87, 40, 41, 49};

              AlsaHelper::bulk_led_value(traktor_device_id_, to_on, Led::ON, 4,
                                         config_helper);
              AlsaHelper::bulk_led_value(traktor_device_id_, to_low,
                                         Led::MIDDLE, 1, config_helper);
              AlsaHelper::bulk_led_value(traktor_device_id_, to_off, Led::OFF,
                                         4, config_helper);
            }
            logger->debug(
                "[EvDevHelper::read_events_from_device] Deck toggle AC "
                "Changed: "
                "{0}",
                toggle_ac);
            continue;
          }
          if ((ev.code ==
               config_helper->get_int_value("alsa_device_toggle_bd_value")) &&
              (ev.value == 1)) {  // TOGGLE CH2 / CH4
            toggle_bd = !toggle_bd;
            if (toggle_bd) {
              int to_on[] = {53, 54, 62, 131, 119};
              int to_off[] = {130, 36, 27, 28};
              AlsaHelper::bulk_led_value(traktor_device_id_, to_on, Led::ON, 5,
                                         config_helper);
              AlsaHelper::bulk_led_value(traktor_device_id_, to_off, Led::OFF,
                                         4, config_helper);
            } else {
              int to_on[] = {27, 28, 36, 130};
              int to_low[] = {119};
              int to_off[] = {131, 62, 53, 54};
              AlsaHelper::bulk_led_value(traktor_device_id_, to_on, Led::ON, 4,
                                         config_helper);
              AlsaHelper::bulk_led_value(traktor_device_id_, to_low,
                                         Led::MIDDLE, 1, config_helper);
              AlsaHelper::bulk_led_value(traktor_device_id_, to_off, Led::OFF,
                                         4, config_helper);
            }
            logger->debug(
                "[EvDevHelper::read_events_from_device] Deck toggle BD "
                "Changed: "
                "{0}",
                toggle_bd);
            continue;
          }
          const char *type_name = libevdev_event_type_get_name(ev.type);
          const char *code_name =
              libevdev_event_code_get_name(ev.type, ev.code);

          if ((type_name != nullptr) && (code_name != nullptr)) {
            logger->debug(
                "[EvDevHelper::read_events_from_device] Event: TypeName: {0} - "
                "CodeName: {1} - Type: {2} - Code: {3} - Value: {4} - Time: "
                "{5}",
                type_name, code_name, ev.type, ev.code, ev.value,
                ev.time.tv_sec);
          } else {
            logger->debug(
                "[EvDevHelper::read_events_from_device] Event: Type: {0} - "
                "Code: "
                "{1} - Value: {2} - Time: {3}",
                ev.type, ev.code, ev.value, ev.time.tv_sec);
          }

          EvDevEvent evdev_event(ev.type, ev.code, ev.value, ev.time);
          evdev_event.handle_with(pMidiOut, traktor_device_id_, shift_ch1,
                                  shift_ch2, toggle_ac, toggle_bd,
                                  config_helper);
        }
      }
    }

    libevdev_free(dev);
    close(fd);
  }
}

void EvDevHelper::initialize_buttons_leds(ConfigHelper *config_helper) {
  shared_ptr<spdlog::logger> logger =
      spdlog::get(config_helper->get_string_value("traktor_s4_logger_name"));
  logger->debug(
      "[EvDevHelper::initialize_buttons_leds] Initializing controller "
      "Leds....");
  int control_ids[Led::leds_mapping.size()];

  logger->debug("[EvDevHelper::initialize_buttons_leds] Using device {0}",
                to_string(traktor_device_id_));
  if (traktor_device_id_ != -1) {
    map<int, Led *>::iterator it;
    int cont = 0;
    for (it = Led::leds_mapping.begin(); it != Led::leds_mapping.end(); it++) {
      if (it->second->by_default) {
        logger->debug(
            "[EvDevHelper::initialize_buttons_leds] Preparing for init Led "
            "with Code {0}",
            to_string(it->second->code));
        control_ids[cont] = it->second->code;
        if (((cont % 5) == 0) && (cont > 0)) {
          AlsaHelper::bulk_led_value(traktor_device_id_, control_ids,
                                     Led::MIDDLE, cont + 1, config_helper);
          cont = 0;
        } else
          cont++;
      }
    }
    AlsaHelper::bulk_led_value(traktor_device_id_, control_ids, Led::MIDDLE,
                               cont + 1, config_helper);
  } else {
    logger->error(
        "[EvDevHelper::initialize_buttons_leds] Cannot use {0} device",
        to_string(traktor_device_id_));
    exit(EXIT_FAILURE);
  }
  logger->debug("[EvDevHelper::initialize_buttons_leds] Finished");
}

void EvDevHelper::shutdown_buttons_leds(ConfigHelper *config_helper) {
  shared_ptr<spdlog::logger> logger =
      spdlog::get(config_helper->get_string_value("traktor_s4_logger_name"));
  logger->debug(
      "[EvDevHelper::shutdown_buttons_leds] Shutting down controller Leds....");
  map<int, Led *>::iterator it;
  int control_ids[Led::leds_mapping.size()];
  int cont = 0;
  for (it = Led::leds_mapping.begin(); it != Led::leds_mapping.end(); it++) {
    control_ids[cont] = it->second->code;
    logger->debug("Preparing for shutdown Led Code {0}",
                  to_string(it->second->code));
    if (((cont % 5) == 0) && (cont > 0)) {
      AlsaHelper::bulk_led_value(traktor_device_id_, control_ids, Led::OFF,
                                 cont + 1, config_helper);
      cont = 0;
    } else
      cont++;
  }
  AlsaHelper::bulk_led_value(traktor_device_id_, control_ids, Led::OFF,
                             cont + 1, config_helper);
  logger->debug("[EvDevHelper::shutdown_buttons_leds] Finished");
}
