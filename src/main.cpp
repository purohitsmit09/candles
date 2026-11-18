#include <iostream>
#include <string>

#include "client.h"
#include "core.h"
#include "db.h"
#include "ingest.h"
#include "server.h"
#include "service.h"

namespace {
const char* const kUsage =
    "usage:\n"
    "  mrq serve           load data/ into candles.db and serve on 127.0.0.1:8080\n"
    "  mrq send <FILE>     send the JSON request in FILE and print the response\n"
    "  mrq send -          read the JSON request from stdin\n"
    "  mrq help\n";
}

int main(int argc, char** argv) {
    if (argc < 2 || std::string(argv[1]) == "help") {
        std::cout << kUsage;
        return 0;
    }
    std::string cmd = argv[1];
    if (cmd == "serve") {
        sqlite3* db = nullptr;
        try {
            db = mrq::db_open(mrq::kDbPath);
        } catch (const std::exception& e) {
            std::cerr << "error: " << e.what() << std::endl;
            return 1;
        }
        mrq::ingest_data_dir(db, mrq::kDataDir);
        mrq::MarketService svc(db);
        int rc = mrq::run_server(svc);
        mrq::db_close(db);
        return rc;
    }
    if (cmd == "send")
        return mrq::run_client(argc, argv);
    std::cerr << kUsage;
    return 2;
}
