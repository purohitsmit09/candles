#include "client.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

#include "core.h"

namespace mrq {

using json = nlohmann::ordered_json;

namespace {

const char* const kUsage =
    "usage:\n"
    "  mrq serve           load data/ into candles.db and serve on 127.0.0.1:8080\n"
    "  mrq send <FILE>     send the JSON request in FILE and print the response\n"
    "  mrq send -          read the JSON request from stdin\n"
    "  mrq help\n";

int usage_error() {
    std::cerr << kUsage;
    return 2;
}

}  // namespace

int run_client(int argc, char** argv) {
    if (argc != 3) return usage_error();
    const std::string arg = argv[2];
    const std::string name = arg == "-" ? "stdin" : arg;
    std::stringstream ss;
    if (arg == "-") {
        ss << std::cin.rdbuf();
    } else {
        std::ifstream in(arg);
        if (!in) {
            std::cerr << "error: cannot read " << arg << std::endl;
            return 2;
        }
        ss << in.rdbuf();
    }
    json req;
    try {
        req = json::parse(ss.str());
    } catch (...) {
        std::cerr << "error: " << name << ": invalid JSON" << std::endl;
        return 2;
    }

    std::string line = req.dump(-1, ' ', false, json::error_handler_t::replace) + "\n";

    signal(SIGPIPE, SIG_IGN);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(kPort);
    inet_pton(AF_INET, kHost, &addr.sin_addr);
    if (fd < 0 || connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        std::cerr << "error: cannot reach server at 127.0.0.1:8080 (is mrq serve running?)" << std::endl;
        if (fd >= 0) close(fd);
        return 1;
    }
    size_t off = 0;
    while (off < line.size()) {
        ssize_t n = write(fd, line.data() + off, line.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        off += static_cast<size_t>(n);
    }
    std::string buf;
    char chunk[4096];
    while (buf.find('\n') == std::string::npos) {
        ssize_t n = read(fd, chunk, sizeof chunk);
        if (n <= 0) break;
        buf.append(chunk, static_cast<size_t>(n));
    }
    close(fd);

    std::string resp = buf.substr(0, buf.find('\n'));
    if (resp.empty()) {
        std::cerr << "error: empty response" << std::endl;
        return 1;
    }
    json parsed;
    try {
        parsed = json::parse(resp);
    } catch (...) {
        std::cerr << "error: invalid response" << std::endl;
        return 1;
    }
    if (parsed.is_object() && parsed.contains("ok") && parsed["ok"] == true) {
        std::cout << resp << std::endl;
        return 0;
    }
    std::cerr << resp << std::endl;
    return 1;
}

}  // namespace mrq
