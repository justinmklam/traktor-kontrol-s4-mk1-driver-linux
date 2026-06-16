#include "PerfCounters.h"
#include "spdlog/spdlog.h"

std::unordered_map<std::string, PerfCounters::Counter>& PerfCounters::slots() {
    static std::unordered_map<std::string, Counter> instances;
    return instances;
}

void PerfCounters::report() {
    auto &s = slots();
    if (s.empty()) return;

    // Try to find the driver logger; fall back to default if not available
    auto logger = spdlog::get("traktor_kontrol_s4_logger");
    if (!logger) logger = spdlog::default_logger();
    if (!logger) return;

    logger->info("=== PerfCounters report ===");
    for (auto &[name, ctr] : s) {
        auto snap = ctr.snapshot();
        if (snap.count == 0) continue;
        int64_t avg_us = snap.sum_us / static_cast<int64_t>(snap.count);
        logger->info("  {:<40s}  count={:>8}  min={:>6}µs  avg={:>6}µs  max={:>6}µs  total={:>10}µs",
                     name.c_str(), snap.count, snap.min_us, avg_us, snap.max_us, snap.sum_us);
    }
    logger->info("============================");
}
