#include "service.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mrq {

namespace {

Error internal_error(const std::exception& e) { return Error{Status::Internal, e.what()}; }

// Uppercases the symbol in place. Returns false if it is empty or not only A-Z / 0-9.
bool normalize_symbol(std::string& symbol) {
    std::transform(symbol.begin(), symbol.end(), symbol.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (symbol.empty()) return false;
    return std::all_of(symbol.begin(), symbol.end(),
                       [](char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); });
}

// Uppercases the symbol and aligns from/to down to the interval.
// Returns an Error if the request is invalid, std::nullopt if it is valid.
std::optional<Error> validate(std::string& symbol, int64_t& from, int64_t& to, int64_t interval_sec) {
    if (!normalize_symbol(symbol))
        return Error{Status::InvalidArgument, "invalid symbol"};
    if (interval_sec == -1)
        return Error{Status::InvalidArgument, "interval must be one of 1m,5m,15m,1h,4h,1d"};
    if (from < 0 || to < 0)
        return Error{Status::InvalidArgument, "from and to must be non-negative"};
    from -= from % interval_sec;
    to -= to % interval_sec;
    if (from >= to)
        return Error{Status::InvalidArgument, "range is shorter than one interval"};
    return std::nullopt;
}

Error not_found(const std::string& symbol, int64_t from, int64_t to) {
    return Error{Status::NotFound, "no data for " + symbol + " in [" + std::to_string(from) + ", " +
                                       std::to_string(to) + ")"};
}

}  // namespace

Result<std::vector<SymbolInfo>> MarketService::symbols() {
    try {
        return db_symbols(db_);
    } catch (const std::exception& e) {
        return std::unexpected(internal_error(e));
    }
}

Result<CandlesResponse> MarketService::candles(CandlesRequest r) {
    try {
        if (std::optional<Error> err = validate(r.symbol, r.from, r.to, r.interval_sec))
            return std::unexpected(*err);

        std::vector<Candle> bars = resample(db_range(db_, r.symbol, r.from, r.to), r.interval_sec);
        if (bars.empty()) return std::unexpected(not_found(r.symbol, r.from, r.to));

        return CandlesResponse{r.symbol, r.interval_sec, r.from, r.to, std::move(bars)};
    } catch (const std::exception& e) {
        return std::unexpected(internal_error(e));
    }
}

Result<SmaResponse> MarketService::sma(SmaRequest r) {
    try {
        if (std::optional<Error> err = validate(r.symbol, r.from, r.to, r.interval_sec))
            return std::unexpected(*err);
        if (r.window < 1 || r.window > kMaxWindow)
            return std::unexpected(Error{Status::InvalidArgument, "window must be between 1 and 1000"});

        // Read `window` extra bars before `from` so the first returned SMA has a full window.
        int64_t read_from = std::max<int64_t>(0, r.from - int64_t(r.window) * r.interval_sec);
        std::vector<Candle> bars = resample(db_range(db_, r.symbol, read_from, r.to), r.interval_sec);

        std::vector<Candle>::iterator first =
            std::lower_bound(bars.begin(), bars.end(), r.from,
                             [](const Candle& c, int64_t t) { return c.ts < t; });
        if (first == bars.end()) return std::unexpected(not_found(r.symbol, r.from, r.to));

        std::vector<double> closes;
        closes.reserve(bars.size());
        for (const Candle& b : bars) closes.push_back(b.close);
        std::vector<std::optional<double>> avg = mrq::sma(closes, r.window);

        std::vector<SmaRow> rows;
        for (size_t i = static_cast<size_t>(first - bars.begin()); i < bars.size(); ++i)
            rows.push_back(SmaRow{bars[i].ts, bars[i].close, avg[i]});

        return SmaResponse{r.symbol, r.interval_sec, r.from, r.to, r.window, std::move(rows)};
    } catch (const std::exception& e) {
        return std::unexpected(internal_error(e));
    }
}

Result<PriceResponse> MarketService::price(PriceRequest r) {
    try {
        if (!normalize_symbol(r.symbol))
            return std::unexpected(Error{Status::InvalidArgument, "invalid symbol"});
        if (r.at < 0)
            return std::unexpected(Error{Status::InvalidArgument, "at must be non-negative"});

        std::optional<Candle> candle = db_last_before(db_, r.symbol, r.at);
        if (!candle)
            return std::unexpected(Error{Status::NotFound, "no data for " + r.symbol +
                                                               " at or before " + std::to_string(r.at)});

        return PriceResponse{r.symbol, r.at, *candle};
    } catch (const std::exception& e) {
        return std::unexpected(internal_error(e));
    }
}

}  // namespace mrq
