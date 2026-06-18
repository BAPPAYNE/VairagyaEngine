#ifndef URL_STATE_H
#define URL_STATE_H

#include "net/fetcher.hpp"

#include <string>
#include <cstdint>

using namespace std;

namespace storage {
    struct URLState {
        string normalized_url;   // PK
        net::FetchStatus fetch_status;
        uint16_t http_status;
        uint8_t retry_count;
        uint64_t last_fetch_ts;
        string content_hash;
    };
};

#endif // URL_STATE_H