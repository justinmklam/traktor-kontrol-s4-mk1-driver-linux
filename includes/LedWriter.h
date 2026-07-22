#ifndef TRAKTOR_KONTROL_S4_MK1_DRIVER_LINUX_LEDWRITER_H
#define TRAKTOR_KONTROL_S4_MK1_DRIVER_LINUX_LEDWRITER_H

// ---
#include <alsa/asoundlib.h>

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
// ---
#include "ConfigHelper.h"

class LedWriter {
 public:
  LedWriter(int card_id, ConfigHelper* config_helper);
  ~LedWriter();

  void set_led(int control_id, int value);
  void set_leds(const std::vector<std::pair<int, int>>& updates);

 private:
  void worker_loop();
  bool open_control();
  void close_control();
  bool write_led(int control_id, int value);

  int card_id_;
  ConfigHelper* config_helper_;
  std::shared_ptr<spdlog::logger> logger_;
  snd_ctl_t* control_ = nullptr;
  std::thread worker_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::map<int, int> pending_updates_;
  std::atomic<bool> running_{true};
  int shadow_state_[164];
};

#endif  // TRAKTOR_KONTROL_S4_MK1_DRIVER_LINUX_LEDWRITER_H
