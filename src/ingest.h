#pragma once

#include "core.h"

#include <optional>
#include <string>

struct sqlite3;

namespace mrq {

std::optional<std::string> symbol_from_filename(const std::string& filename);
bool parse_line(const std::string& line, Candle& out);   // false = malformed
void ingest_data_dir(sqlite3* db, const std::string& dir);

}  // namespace mrq
