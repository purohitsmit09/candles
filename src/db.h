#pragma once

#include "core.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace mrq {

struct SymbolInfo {
    std::string symbol;
    int64_t from;   // first candle ts
    int64_t to;     // last candle ts + 60
    int64_t rows;   // number of 1-minute candles
};

// Opens (creating if needed) and ensures the schema. Throws std::runtime_error on failure.
sqlite3* db_open(const char* path);
void     db_close(sqlite3* db);

std::vector<SymbolInfo> db_symbols(sqlite3* db);
std::vector<Candle>     db_range(sqlite3* db, const std::string& symbol, int64_t from, int64_t to);  // [from, to)

// Upserts all candles for one symbol in a single transaction.
// All-or-nothing: on failure rolls back and throws std::runtime_error.
void db_upsert(sqlite3* db, const std::string& symbol, const std::vector<Candle>& candles);

// Last candle with ts + 60 <= at (completed at or before `at`), or nullopt if none.
std::optional<Candle> db_last_before(sqlite3* db, const std::string& symbol, int64_t at);

}  // namespace mrq
