#ifndef TRAKTOR_KONTROL_S4_MK1_DRIVER_LINUX_LEDWRITER_H
#define TRAKTOR_KONTROL_S4_MK1_DRIVER_LINUX_LEDWRITER_H

// ---------------------------------------------------------------------------
// Asynchronous LED writer module.
//
// Owns the ALSA ctl handle and a single worker thread that drains a queue
// of LED update commands.  The hot path (MIDI callback, evdev loop) calls
// set_led()/set_leds() which enqueue and return immediately.
//
// A shadow-state array skips duplicate snd_ctl_elem_write calls so that
// setting an LED to its current value is a no-op.
// ---------------------------------------------------------------------------

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <utility>
#include <atomic>
#include <cstdint>
#ifndef REPLAY_MODE
#include <alsa/asoundlib.h>
#else
struct snd_ctl;
typedef snd_ctl snd_ctl_t;
#endif

class LedWriter {
public:
    /// Opens the ALSA ctl handle for the given card and starts the worker.
    explicit LedWriter(int card_id);

    /// Signals the worker to stop, drains remaining work, closes the ctl handle.
    ~LedWriter();

    // Non-copyable, non-movable.
    LedWriter(const LedWriter&) = delete;
    LedWriter& operator=(const LedWriter&) = delete;

    /// Enqueue a single LED update.  Returns immediately.
    void set_led(int control_id, int value);

    /// Enqueue a batch of LED updates.  Returns immediately.
    void set_leds(const std::vector<std::pair<int, int>>& updates);

    /// Block until all previously-enqueued work has been written to hardware.
    void flush();

private:
    void worker_loop();

    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<std::vector<std::pair<int, int>>> queue_;
    std::atomic<bool> running_{true};

    /// Shadow state: shadow_state_[id] caches the last-written value for
    /// LED id.  Index 0 is unused (LED IDs start at 1).
    int shadow_state_[164] = {};

    snd_ctl_t* ctl_ = nullptr;
    int card_id_ = -1;
};

#endif // TRAKTOR_KONTROL_S4_MK1_DRIVER_LINUX_LEDWRITER_H
