#include "LedWriter.h"

// ---
#include <algorithm>
#include <string>
// ---
#include "Led.h"
#include "spdlog/spdlog.h"

LedWriter::LedWriter(int card_id, ConfigHelper* config_helper)
    : card_id_(card_id),
      logger_(spdlog::get(
          config_helper->get_string_value("traktor_s4_logger_name"))) {
  std::fill(std::begin(shadow_state_), std::end(shadow_state_), -1);
  worker_ = std::thread(&LedWriter::worker_loop, this);
}

LedWriter::~LedWriter() {
  running_ = false;
  cv_.notify_one();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void LedWriter::set_led(int control_id, int value) {
  set_leds({{control_id, value}});
}

void LedWriter::set_leds(
    const std::vector<std::pair<int, int>>& updates) {
  if (!running_) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& update : updates) {
      if (Led::leds_mapping.find(update.first) != Led::leds_mapping.end()) {
        pending_updates_[update.first] = update.second;
      }
    }
  }
  cv_.notify_one();
}

void LedWriter::worker_loop() {
  while (true) {
    std::map<int, int> updates;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] {
        return !running_ || !pending_updates_.empty();
      });
      updates.swap(pending_updates_);
    }

    if (updates.empty()) {
      if (!running_) {
        break;
      }
      continue;
    }

    if (!open_control()) {
      continue;
    }

    for (const auto& update : updates) {
      if (shadow_state_[update.first] == update.second) {
        continue;
      }
      write_led(update.first, update.second);
    }
  }

  close_control();
}

bool LedWriter::open_control() {
  if (control_ != nullptr) {
    return true;
  }

  const std::string control_name = "hw:" + std::to_string(card_id_);
  if (snd_ctl_open(&control_, control_name.c_str(), SND_CTL_NONBLOCK) < 0) {
    logger_->error("[LedWriter::open_control] Unable to open ALSA control {0}",
                   control_name);
    control_ = nullptr;
    return false;
  }
  return true;
}

void LedWriter::close_control() {
  if (control_ != nullptr) {
    snd_ctl_close(control_);
    control_ = nullptr;
  }
}

void LedWriter::write_led(int control_id, int value) {
  snd_ctl_elem_value_t* control_value;
  snd_ctl_elem_value_alloca(&control_value);
  snd_ctl_elem_value_set_interface(control_value, SND_CTL_ELEM_IFACE_MIXER);
  snd_ctl_elem_value_set_numid(control_value, control_id);
  snd_ctl_elem_value_set_integer(control_value, 0, value);

  const int result = snd_ctl_elem_write(control_, control_value);
  if (result < 0) {
    logger_->error("[LedWriter::write_led] Unable to write LED {0}: {1}",
                   control_id, snd_strerror(result));
    return;
  }
  shadow_state_[control_id] = value;
}
