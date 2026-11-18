#pragma once

#include "core.h"
#include "db.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace mrq {

enum class Status { InvalidArgument, NotFound, Internal };

struct Error {
    Status      status;
    std::string message;
};

template <class T>
using Result = std::expected<T, Error>;

struct CandlesRequest {
    std::string symbol;
    int64_t     from;
    int64_t     to;
    int64_t     interval_sec;   // -1 if the interval name was not recognized
};

struct CandlesResponse {
    std::string         symbol;
    int64_t             interval_sec;
    int64_t             from;   // aligned
    int64_t             to;     // aligned
    std::vector<Candle> bars;
};

struct SmaRequest {
    std::string symbol;
    int64_t     from;
    int64_t     to;
    int64_t     interval_sec;   // -1 if not recognized
    int         window;
};

struct SmaRow {
    int64_t               ts;
    double                close;
    std::optional<double> sma;
};

struct SmaResponse {
    std::string         symbol;
    int64_t             interval_sec;
    int64_t             from;   // aligned
    int64_t             to;     // aligned
    int                 window;
    std::vector<SmaRow> rows;
};

struct PriceRequest {
    std::string symbol;
    int64_t     at;
};

struct PriceResponse {
    std::string symbol;   // uppercased
    int64_t     at;
    Candle      candle;   // last candle with candle.ts + 60 <= at
};

class MarketService {
public:
    explicit MarketService(sqlite3* db) : db_(db) {}
    Result<std::vector<SymbolInfo>> symbols();
    Result<CandlesResponse>         candles(CandlesRequest r);
    Result<SmaResponse>             sma(SmaRequest r);
    Result<PriceResponse>           price(PriceRequest r);

private:
    sqlite3* db_;
};

}  // namespace mrq
