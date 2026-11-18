#include "server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <csignal>
#include <cstring>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>

namespace mrq {

using json = nlohmann::ordered_json;

namespace {

std::string dump(const json& j) {
    return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

const char* code_name(Status s) {
    switch (s) {
        case Status::InvalidArgument: return "invalid_argument";
        case Status::NotFound:        return "not_found";
        default:                      return "internal";
    }
}

json error_json(const json& id, const char* code, const std::string& msg) {
    json j;
    j["ok"] = false;
    if (!id.is_null()) j["id"] = id;
    j["code"]  = code;
    j["error"] = msg;
    return j;
}

std::string bad_request(const json& id, const std::string& msg) {
    return dump(error_json(id, "invalid_argument", msg));
}

json ok_json(const json& id) {
    json j;
    j["ok"] = true;
    if (!id.is_null()) j["id"] = id;
    return j;
}

json to_json(const std::vector<SymbolInfo>& v, const json& id) {
    json j = ok_json(id);
    json arr = json::array();
    for (const SymbolInfo& s : v) {
        json o;
        o["symbol"] = s.symbol;
        o["from"]   = s.from;
        o["to"]     = s.to;
        o["rows"]   = s.rows;
        arr.push_back(o);
    }
    j["symbols"] = arr;
    return j;
}

json to_json(const CandlesResponse& r, const json& id) {
    json j = ok_json(id);
    j["symbol"]   = r.symbol;
    j["interval"] = interval_name(r.interval_sec);
    j["from"]     = r.from;
    j["to"]       = r.to;
    j["columns"]  = {"ts", "open", "high", "low", "close", "volume", "trades"};
    json rows = json::array();
    for (const Candle& c : r.bars)
        rows.push_back(json::array({c.ts, c.open, c.high, c.low, c.close, c.volume, c.trades}));
    j["rows"] = rows;
    return j;
}

json to_json(const SmaResponse& r, const json& id) {
    json j = ok_json(id);
    j["symbol"]   = r.symbol;
    j["interval"] = interval_name(r.interval_sec);
    j["from"]     = r.from;
    j["to"]       = r.to;
    j["window"]   = r.window;
    j["columns"]  = {"ts", "close", "sma"};
    json rows = json::array();
    for (const SmaRow& s : r.rows) {
        json v = s.sma ? json(*s.sma) : json(nullptr);
        rows.push_back(json::array({s.ts, s.close, v}));
    }
    j["rows"] = rows;
    return j;
}

json to_json(const PriceResponse& r, const json& id) {
    json j = ok_json(id);
    j["symbol"] = r.symbol;
    j["at"]     = r.at;
    json c;
    c["ts"]     = r.candle.ts;
    c["open"]   = r.candle.open;
    c["high"]   = r.candle.high;
    c["low"]    = r.candle.low;
    c["close"]  = r.candle.close;
    c["volume"] = r.candle.volume;
    c["trades"] = r.candle.trades;
    j["candle"] = c;
    return j;
}

template <class T>
json result_json(const Result<T>& r, const json& id) {
    if (!r) return error_json(id, code_name(r.error().status), r.error().message);
    return to_json(*r, id);
}

int64_t interval_from(const json& req) {
    if (!req.contains("interval")) return 60;
    std::optional<int64_t> s = interval_seconds(req["interval"].get<std::string>());
    return s ? *s : -1;
}

CandlesRequest candles_request_from(const json& req) {
    return CandlesRequest{req["symbol"].get<std::string>(), req["from"].get<int64_t>(),
                          req["to"].get<int64_t>(), interval_from(req)};
}

SmaRequest sma_request_from(const json& req) {
    int64_t w = req["window"].get<int64_t>();
    int window = (w < std::numeric_limits<int>::min() || w > std::numeric_limits<int>::max())
                     ? 0
                     : static_cast<int>(w);
    return SmaRequest{req["symbol"].get<std::string>(), req["from"].get<int64_t>(),
                      req["to"].get<int64_t>(), interval_from(req), window};
}

bool write_all(int fd, const std::string& s) {
    size_t off = 0;
    while (off < s.size()) {
        ssize_t n = ::write(fd, s.data() + off, s.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        off += static_cast<size_t>(n);
    }
    return true;
}

}  // namespace

std::string handle_request(const std::string& line, MarketService& svc) {
    json req;
    try {
        req = json::parse(line);
    } catch (const json::parse_error&) {
        return bad_request(nullptr, "invalid JSON");
    }
    if (!req.is_object()) return bad_request(nullptr, "request must be a JSON object");

    json id;  // null
    if (req.contains("id")) {
        if (!req["id"].is_string() && !req["id"].is_number_integer())
            return bad_request(nullptr, "id must be a string or integer");
        id = req["id"];
    }

    if (!req.contains("cmd")) return bad_request(id, "missing field: cmd");
    if (!req["cmd"].is_string()) return bad_request(id, "field cmd must be a string");
    const std::string cmd = req["cmd"].get<std::string>();
    if (cmd != "symbols" && cmd != "candles" && cmd != "sma" && cmd != "price")
        return bad_request(id, "unknown cmd: " + cmd);

    std::vector<std::string> allowed = {"cmd", "id"};
    std::vector<std::string> required;
    if (cmd == "candles" || cmd == "sma") {
        for (const char* k : {"symbol", "from", "to", "interval"}) allowed.push_back(k);
        required = {"symbol", "from", "to"};
    }
    if (cmd == "sma") {
        allowed.push_back("window");
        required.push_back("window");
    }
    if (cmd == "price") {
        allowed.push_back("symbol");
        allowed.push_back("at");
        required = {"symbol", "at"};
    }
    for (json::iterator it = req.begin(); it != req.end(); ++it)
        if (std::find(allowed.begin(), allowed.end(), it.key()) == allowed.end())
            return bad_request(id, "unknown field: " + it.key());
    for (const std::string& k : required)
        if (!req.contains(k)) return bad_request(id, "missing field: " + k);

    for (const char* k : {"symbol", "interval"})
        if (req.contains(k) && !req[k].is_string()) return bad_request(id, std::string("field ") + k + " must be a string");
    for (const char* k : {"from", "to", "window", "at"})
        if (req.contains(k) && !req[k].is_number_integer())
            return bad_request(id, std::string("field ") + k + " must be an integer");

    if (cmd == "symbols") return dump(result_json(svc.symbols(), id));
    if (cmd == "candles") return dump(result_json(svc.candles(candles_request_from(req)), id));
    if (cmd == "price")
        return dump(result_json(
            svc.price(PriceRequest{req["symbol"].get<std::string>(), req["at"].get<int64_t>()}), id));
    return dump(result_json(svc.sma(sma_request_from(req)), id));
}

int run_server(MarketService& svc) {
    signal(SIGPIPE, SIG_IGN);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    bool ok = fd >= 0;
    if (ok) {
        int one = 1;
        ok = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) == 0;
    }
    if (ok) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(kPort);
        ok = inet_pton(AF_INET, kHost, &addr.sin_addr) == 1 &&
             bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0;
    }
    if (ok) ok = listen(fd, 16) == 0;
    if (!ok) {
        std::cerr << "error: cannot listen on 127.0.0.1:8080: " << std::strerror(errno) << std::endl;
        if (fd >= 0) close(fd);
        return 1;
    }
    std::cout << "serving on 127.0.0.1:8080" << std::endl;

    while (true) {
        int cfd = accept(fd, nullptr, nullptr);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            std::cerr << "error: accept: " << std::strerror(errno) << std::endl;
            continue;
        }
        timeval tv{kTimeoutSec, 0};
        setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        try {
            std::string buf;
            char chunk[1024];
            while (buf.find('\n') == std::string::npos && buf.size() < kMaxLine) {
                size_t want = std::min(sizeof chunk, kMaxLine - buf.size());
                ssize_t n = read(cfd, chunk, want);
                if (n <= 0) break;
                buf.append(chunk, static_cast<size_t>(n));
            }
            size_t nl = buf.find('\n');
            if (nl == std::string::npos && buf.empty()) {
                close(cfd);
                continue;
            }
            std::string response;
            if (nl == std::string::npos && buf.size() >= kMaxLine) {
                response = dump(error_json(nullptr, "invalid_argument", "request too long"));
            } else {
                std::string line = buf.substr(0, nl);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                response = handle_request(line, svc);
            }
            write_all(cfd, response + "\n");
        } catch (const std::exception& e) {
            write_all(cfd, dump(error_json(nullptr, "internal", e.what())) + "\n");
        }
        close(cfd);
    }
}

}  // namespace mrq
