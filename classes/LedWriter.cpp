#include "LedWriter.h"
#include "spdlog/spdlog.h"

using namespace std;

LedWriter::LedWriter(int card_id) : card_id_(card_id) {
    string control_name = "hw:" + to_string(card_id);
    if (snd_ctl_open(&ctl_, control_name.c_str(), SND_CTL_NONBLOCK) < 0) {
        ctl_ = nullptr;
        auto logger = spdlog::get("traktor_kontrol_s4_logger");
        if (logger) logger->error("[LedWriter] Failed to open ALSA ctl handle for {0}", control_name);
        return;
    }
    auto logger = spdlog::get("traktor_kontrol_s4_logger");
    if (logger) logger->info("[LedWriter] ALSA ctl handle opened for {0}", control_name);
    worker_ = thread(&LedWriter::worker_loop, this);
}

LedWriter::~LedWriter() {
    running_.store(false, memory_order_relaxed);
    {
        lock_guard<mutex> lock(mutex_);
        cv_.notify_all();
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    if (ctl_ != nullptr) {
        snd_ctl_close(ctl_);
        ctl_ = nullptr;
        auto logger = spdlog::get("traktor_kontrol_s4_logger");
        if (logger) logger->info("[LedWriter] ALSA ctl handle closed");
    }
}

void LedWriter::set_led(int control_id, int value) {
    set_leds({{control_id, value}});
}

void LedWriter::set_leds(const vector<pair<int, int>>& updates) {
    if (!running_.load(memory_order_relaxed) || ctl_ == nullptr) {
        return;
    }
    {
        lock_guard<mutex> lock(mutex_);
        queue_.emplace(updates);
    }
    cv_.notify_one();
}

void LedWriter::flush() {
    // Wait until the queue is empty and the worker has finished processing.
    unique_lock<mutex> lock(mutex_);
    cv_.wait(lock, [this] { return queue_.empty(); });
}

void LedWriter::worker_loop() {
    snd_ctl_elem_value_t *value;
    snd_ctl_elem_value_alloca(&value);
    snd_ctl_elem_value_set_interface(value, SND_CTL_ELEM_IFACE_MIXER);

    while (running_.load(memory_order_relaxed)) {
        vector<pair<int, int>> batch;
        {
            unique_lock<mutex> lock(mutex_);
            cv_.wait(lock, [this] {
                return !queue_.empty() || !running_.load(memory_order_relaxed);
            });
            if (!running_.load(memory_order_relaxed) && queue_.empty()) {
                return;
            }
            batch = std::move(queue_.front());
            queue_.pop();
        }

        for (auto& [control_id, led_value] : batch) {
            if (control_id < 1 || control_id > 163) continue;
            // §3: Shadow state — skip duplicate writes
            if (shadow_state_[control_id] == led_value) continue;
            shadow_state_[control_id] = led_value;

            if (ctl_ == nullptr) continue;

            snd_ctl_elem_value_set_numid(value, control_id);
            snd_ctl_elem_value_set_integer(value, 0, led_value);
            snd_ctl_elem_write(ctl_, value);
        }
    }
}
