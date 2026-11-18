#include "ingest.h"

#include "db.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace mrq {

std::optional<std::string> symbol_from_filename(const std::string& filename) {
    if (filename.size() < 4 || filename.compare(filename.size() - 4, 4, ".csv") != 0)
        return std::nullopt;
    size_t d1 = filename.find('-');
    if (d1 == std::string::npos || d1 == 0) return std::nullopt;
    std::string sym = filename.substr(0, d1);
    for (char c : sym)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return std::nullopt;
    size_t d2 = filename.find('-', d1 + 1);
    if (filename.substr(d1 + 1, d2 == std::string::npos ? std::string::npos : d2 - d1 - 1) != "1m")
        return std::nullopt;
    return sym;
}

bool parse_line(const std::string& raw_line, Candle& out) {
    std::string line = raw_line;
    if (!line.empty() && line.back() == '\r') line.pop_back();

    std::vector<std::string> f;
    std::stringstream ss(line);
    for (std::string field; std::getline(ss, field, ',');) f.push_back(field);
    if (f.size() < 9) return false;

    try {
        int64_t raw = std::stoll(f[0]);
        int64_t ts  = raw > 1'000'000'000'000'000 ? raw / 1'000'000   // microseconds (2025+)
                    : raw > 1'000'000'000'000     ? raw / 1'000       // milliseconds
                    : raw;                                            // seconds
        out = Candle{ts, std::stod(f[1]), std::stod(f[2]), std::stod(f[3]),
                     std::stod(f[4]), std::stod(f[5]), std::stoll(f[8])};
    } catch (...) {
        return false;
    }
    return out.low <= out.high;
}

void ingest_data_dir(sqlite3* db, const std::string& dir) {
    if (!fs::exists(dir)) {
        std::cout << "ingest: data/ not found, nothing to load\n";
        return;
    }

    std::vector<fs::path> files;
    for (const fs::directory_entry& e : fs::directory_iterator(dir))
        if (e.is_regular_file() && e.path().extension() == ".csv") files.push_back(e.path());
    std::sort(files.begin(), files.end());

    int loaded = 0, failed = 0;
    for (const fs::path& path : files) {
        const std::string name = path.filename().string();

        std::optional<std::string> symbol = symbol_from_filename(name);
        if (!symbol) {
            std::cout << "ingest: " << name << " failed: unrecognized filename\n";
            ++failed;
            continue;
        }
        std::ifstream in(path);
        if (!in) {
            std::cout << "ingest: " << name << " failed: cannot open file\n";
            ++failed;
            continue;
        }

        std::vector<Candle> candles;
        int bad = 0;
        for (std::string line; std::getline(in, line);) {
            Candle c;
            if (parse_line(line, c)) candles.push_back(c);
            else ++bad;
        }
        if (candles.empty()) {
            std::cout << "ingest: " << name << " failed: no valid rows\n";
            ++failed;
            continue;
        }

        try {
            db_upsert(db, *symbol, candles);
        } catch (const std::exception& e) {
            std::cout << "ingest: " << name << " failed: " << e.what() << "\n";
            ++failed;
            continue;
        }
        std::cout << "ingest: " << name << " loaded " << candles.size() << " rows";
        if (bad > 0) std::cout << " (" << bad << " bad lines skipped)";
        std::cout << "\n";
        ++loaded;
    }
    std::cout << "ingest: " << loaded << " loaded, " << failed << " failed\n";
}

}  // namespace mrq
