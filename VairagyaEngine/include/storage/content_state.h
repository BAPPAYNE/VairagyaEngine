#ifndef CONTENT_STATE_H
#define CONTENT_STATE_H

#include <string>
#include <cstdint>

using namespace std;

namespace storage {
    struct ContentState {
        string content_hash;   // PK
        string first_seen_url;
        uint64_t first_seen_ts;
        uint32_t seen_count;
    };
}

#endif // CONTENT_STATE_H