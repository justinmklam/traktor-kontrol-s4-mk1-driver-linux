#ifndef TRAKTOR_KONTROL_S4_MK1_DRIVER_LINUX_PERF_COUNTERS_H
#define TRAKTOR_KONTROL_S4_MK1_DRIVER_LINUX_PERF_COUNTERS_H

// ---------------------------------------------------------------------------
// Timing instrumentation for the evdev input → MIDI/LED output path.
// All counters are no-ops when ENABLE_PERF_COUNTERS is *not* defined.
//
// Usage:
//   // At the top of a function or scope:
//   PERF_SCOPE("my_scope");
//
//   // Manually record a duration:
//   auto t0 = PerfCounters::now();
//   // ... work ...
//   PerfCounters::record("alsa_write", t0);
//
// At shutdown, call PerfCounters::report() to dump min/max/avg/count to spdlog.
// ---------------------------------------------------------------------------

#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <chrono>

#ifdef ENABLE_PERF_COUNTERS
#  define PERF_SCOPE(name)  PerfScope _ps_##__LINE__(name)
#else
#  define PERF_SCOPE(name)  ((void)0)
#endif

// ---------------------------------------------------------------------------
// PerfCounters — one counter per named slot, thread-safe.
// ---------------------------------------------------------------------------
class PerfCounters {
public:
    struct Snapshot {
        int64_t min_us   = INT64_MAX;
        int64_t max_us   = 0;
        int64_t sum_us   = 0;
        uint64_t count   = 0;
    };

    /// Returns a monotonic timestamp in microseconds (steady clock).
    static int64_t now() {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }

    /// Record one observation (duration in microseconds).
    static void record(const char *name, int64_t start_us) {
#ifdef ENABLE_PERF_COUNTERS
        auto dur = now() - start_us;
        auto &slot = slots()[name];
        slot.update(dur);
#endif
    }

    /// Dump all counters to spdlog (call at shutdown or on demand).
    static void report();

private:
    struct alignas(64) Counter {          // cache-line-aligned to avoid false sharing
        std::atomic<int64_t> min_us{INT64_MAX};
        std::atomic<int64_t> max_us{0};
        std::atomic<int64_t> sum_us{0};
        std::atomic<uint64_t> count{0};

        void update(int64_t dur) noexcept {
            auto c = count.fetch_add(1, std::memory_order_relaxed) + 1;
            sum_us.fetch_add(dur, std::memory_order_relaxed);
            // min/max via CAS loop
            int64_t old_min = min_us.load(std::memory_order_relaxed);
            while (dur < old_min && !min_us.compare_exchange_weak(old_min, dur, std::memory_order_relaxed)) {}
            int64_t old_max = max_us.load(std::memory_order_relaxed);
            while (dur > old_max && !max_us.compare_exchange_weak(old_max, dur, std::memory_order_relaxed)) {}
            (void)c;
        }

        Snapshot snapshot() const {
            Snapshot s;
            s.min_us = min_us.load(std::memory_order_relaxed);
            s.max_us = max_us.load(std::memory_order_relaxed);
            s.sum_us = sum_us.load(std::memory_order_relaxed);
            s.count  = count.load(std::memory_order_relaxed);
            return s;
        }
    };

    static std::unordered_map<std::string, Counter>& slots();
};

// ---------------------------------------------------------------------------
// RAII scope timer — records duration of the enclosing scope.
// ---------------------------------------------------------------------------
class PerfScope {
public:
    explicit PerfScope(const char *name) : name_(name), start_(PerfCounters::now()) {}
    ~PerfScope() { PerfCounters::record(name_, start_); }
private:
    const char *name_;
    int64_t start_;
};

#endif // TRAKTOR_KONTROL_S4_MK1_DRIVER_LINUX_PERF_COUNTERS_H
