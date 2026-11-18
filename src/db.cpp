#include "db.h"

#include <sqlite3.h>

#include <memory>
#include <stdexcept>

namespace mrq {

namespace {

const char* const kSchema = R"(
    CREATE TABLE IF NOT EXISTS candles (
        symbol TEXT NOT NULL, ts INTEGER NOT NULL,
        open REAL NOT NULL, high REAL NOT NULL, low REAL NOT NULL, close REAL NOT NULL,
        volume REAL NOT NULL, trades INTEGER NOT NULL,
        PRIMARY KEY (symbol, ts)
    ) WITHOUT ROWID;)";

const char* const kUpsert = R"(
    INSERT INTO candles (symbol, ts, open, high, low, close, volume, trades)
    VALUES (@symbol, @ts, @open, @high, @low, @close, @volume, @trades)
    ON CONFLICT (symbol, ts) DO UPDATE SET
        open = excluded.open, high = excluded.high, low = excluded.low,
        close = excluded.close, volume = excluded.volume, trades = excluded.trades;)";

const char* const kRange = R"(
    SELECT ts, open, high, low, close, volume, trades FROM candles
    WHERE symbol = @symbol AND ts >= @from AND ts < @to ORDER BY ts;)";

const char* const kLastBefore = R"(
    SELECT ts, open, high, low, close, volume, trades FROM candles
    WHERE symbol = @symbol AND ts <= @at - 60
    ORDER BY ts DESC LIMIT 1;)";

const char* const kSymbols = R"(
    SELECT symbol, MIN(ts), MAX(ts) + 60, COUNT(*) FROM candles
    GROUP BY symbol ORDER BY symbol;)";

// Finalizes automatically, including during exception unwinding.
using Stmt = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

[[noreturn]] void fail(sqlite3* db) {
    throw std::runtime_error(std::string("sqlite: ") + sqlite3_errmsg(db));
}

Stmt prepare(sqlite3* db, const char* sql) {
    sqlite3_stmt* s = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK) fail(db);
    return Stmt(s, &sqlite3_finalize);
}

void exec(sqlite3* db, const char* sql) {
    if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK) fail(db);
}

// Named-parameter binding.
void bind(sqlite3_stmt* s, const char* name, int64_t v) {
    sqlite3_bind_int64(s, sqlite3_bind_parameter_index(s, name), v);
}
void bind(sqlite3_stmt* s, const char* name, double v) {
    sqlite3_bind_double(s, sqlite3_bind_parameter_index(s, name), v);
}
void bind(sqlite3_stmt* s, const char* name, const std::string& v) {
    sqlite3_bind_text(s, sqlite3_bind_parameter_index(s, name), v.c_str(), -1, SQLITE_TRANSIENT);
}

}  // namespace

sqlite3* db_open(const char* path) {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK ||
        sqlite3_exec(db, kSchema, nullptr, nullptr, nullptr) != SQLITE_OK) {
        std::string msg = sqlite3_errmsg(db);
        sqlite3_close(db);
        throw std::runtime_error("cannot open database: " + msg);
    }
    return db;
}

void db_close(sqlite3* db) { sqlite3_close(db); }

std::vector<SymbolInfo> db_symbols(sqlite3* db) {
    Stmt s = prepare(db, kSymbols);
    std::vector<SymbolInfo> out;
    int rc;
    while ((rc = sqlite3_step(s.get())) == SQLITE_ROW) {
        out.push_back({reinterpret_cast<const char*>(sqlite3_column_text(s.get(), 0)),
                       sqlite3_column_int64(s.get(), 1),
                       sqlite3_column_int64(s.get(), 2),
                       sqlite3_column_int64(s.get(), 3)});
    }
    if (rc != SQLITE_DONE) fail(db);
    return out;
}

std::vector<Candle> db_range(sqlite3* db, const std::string& symbol, int64_t from, int64_t to) {
    Stmt s = prepare(db, kRange);
    bind(s.get(), "@symbol", symbol);
    bind(s.get(), "@from", from);
    bind(s.get(), "@to", to);

    std::vector<Candle> out;
    int rc;
    while ((rc = sqlite3_step(s.get())) == SQLITE_ROW) {
        out.push_back({sqlite3_column_int64(s.get(), 0),
                       sqlite3_column_double(s.get(), 1),
                       sqlite3_column_double(s.get(), 2),
                       sqlite3_column_double(s.get(), 3),
                       sqlite3_column_double(s.get(), 4),
                       sqlite3_column_double(s.get(), 5),
                       sqlite3_column_int64(s.get(), 6)});
    }
    if (rc != SQLITE_DONE) fail(db);
    return out;
}

void db_upsert(sqlite3* db, const std::string& symbol, const std::vector<Candle>& candles) {
    exec(db, "BEGIN");
    try {
        Stmt s = prepare(db, kUpsert);
        bind(s.get(), "@symbol", symbol);  // bindings survive sqlite3_reset
        for (const Candle& c : candles) {
            bind(s.get(), "@ts", c.ts);
            bind(s.get(), "@open", c.open);
            bind(s.get(), "@high", c.high);
            bind(s.get(), "@low", c.low);
            bind(s.get(), "@close", c.close);
            bind(s.get(), "@volume", c.volume);
            bind(s.get(), "@trades", c.trades);
            if (sqlite3_step(s.get()) != SQLITE_DONE) fail(db);
            sqlite3_reset(s.get());
        }
        exec(db, "COMMIT");
    } catch (...) {
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}

std::optional<Candle> db_last_before(sqlite3* db, const std::string& symbol, int64_t at) {
    Stmt s = prepare(db, kLastBefore);
    bind(s.get(), "@symbol", symbol);
    bind(s.get(), "@at", at);

    int rc = sqlite3_step(s.get());
    if (rc == SQLITE_DONE) return std::nullopt;
    if (rc != SQLITE_ROW) fail(db);
    return Candle{sqlite3_column_int64(s.get(), 0),
                  sqlite3_column_double(s.get(), 1),
                  sqlite3_column_double(s.get(), 2),
                  sqlite3_column_double(s.get(), 3),
                  sqlite3_column_double(s.get(), 4),
                  sqlite3_column_double(s.get(), 5),
                  sqlite3_column_int64(s.get(), 6)};
}

}  // namespace mrq
