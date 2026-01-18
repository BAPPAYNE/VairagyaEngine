#ifndef HOST_STATE_H
#define HOST_STATE_H

#include <string>
#include <cstdint>

using namespace std;

namespace storage {
    struct HostState {
        string host;              // PK
        uint64_t robots_fetch_ts;
        uint32_t crawl_delay_ms;
        uint64_t last_request_ts;
        bool robots_allowed;
    };
};

#endif // HOST_STATE_H