#pragma once
#include <string>

#include "service.h"

namespace mrq {

int         run_server(MarketService& svc);
std::string handle_request(const std::string& line, MarketService& svc);

}  // namespace mrq
