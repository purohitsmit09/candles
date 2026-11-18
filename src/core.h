#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mrq {

const char* const kHost       = "127.0.0.1";
const int         kPort       = 8080;
const char* const kDbPath     = "candles.db";
const char* const kDataDir    = "data";
const int         kMaxWindow  = 1000;   // max SMA window (bars)
const size_t      kMaxLine    = 4096;   // max request line bytes
const int         kTimeoutSec = 5;      // server read timeout per connection

struct Candle {
    int64_t ts;      // bar open time, epoch seconds UTC
    double  open;
    double  high;
    double  low;
    double  close;
    double  volume;
    int64_t trades;
};

std::optional<int64_t> interval_seconds(std::string_view name);  // unknown -> nullopt
std::string            interval_name(int64_t seconds);           // input is always valid

std::vector<Candle> resample(const std::vector<Candle>& minutes, int64_t interval_sec);
std::vector<std::optional<double>> sma(const std::vector<double>& values, int window);

}  // namespace mrq
