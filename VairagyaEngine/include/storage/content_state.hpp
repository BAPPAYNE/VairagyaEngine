#ifndef CONTENT_STATE_H
#define CONTENT_STATE_H

#include <string>
#include <cstdint>

using namespace std;

namespace storage {
    struct ContentState {
		string content_hash;   // PK - hash of the content, used for deduplication
        string first_seen_url;
        uint64_t first_seen_ts;
        uint32_t seen_count;
    };
}

#endif // CONTENT_STATE_H