#include "core.h"

#include <algorithm>

namespace mrq {

namespace {
struct IntervalDef {
    const char* name;
    int64_t     seconds;
};
const IntervalDef kIntervals[] = {
    {"1m", 60}, {"5m", 300}, {"15m", 900}, {"1h", 3600}, {"4h", 14400}, {"1d", 86400},
};
}  // namespace

std::optional<int64_t> interval_seconds(std::string_view name) {
    for (const IntervalDef& d : kIntervals)
        if (name == d.name) return d.seconds;
    return std::nullopt;
}

std::string interval_name(int64_t seconds) {
    for (const IntervalDef& d : kIntervals)
        if (d.seconds == seconds) return d.name;
    return "";
}

std::vector<Candle> resample(const std::vector<Candle>& minutes, int64_t interval_sec) {
    if (interval_sec == 60) return minutes;
    std::vector<Candle> out;
    bool    have = false;
    int64_t key  = 0;
    Candle  bar{};
    for (const Candle& m : minutes) {
        int64_t k = m.ts - m.ts % interval_sec;
        if (have && k == key) {
            bar.high = std::max(bar.high, m.high);
            bar.low  = std::min(bar.low, m.low);
            bar.close = m.close;
            bar.volume += m.volume;
            bar.trades += m.trades;
        } else {
            if (have) out.push_back(bar);
            bar = m;
            bar.ts = k;
            key = k;
            have = true;
        }
    }
    if (have) out.push_back(bar);
    return out;
}

std::vector<std::optional<double>> sma(const std::vector<double>& values, int window) {
    std::vector<std::optional<double>> out(values.size());
    double sum = 0;
    for (size_t i = 0; i < values.size(); ++i) {
        sum += values[i];
        if (i >= static_cast<size_t>(window)) sum -= values[i - window];
        if (i >= static_cast<size_t>(window) - 1) out[i] = sum / window;
    }
    return out;
}

}  // namespace mrq
