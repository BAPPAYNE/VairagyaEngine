#ifndef MEMORY_URL_STATE_STORE_H
#define MEMORY_URL_STATE_STORE_H

#include "storage/url_state_store.h"

#include <unordered_map>
#include <mutex>
#include <cstdint>
#include <string>

using namespace std;
using namespace storage;

namespace storage {
    class MemoryURLStateStore : public URLStateStore {
    public:
        MemoryURLStateStore() = default;
        ~MemoryURLStateStore() override = default;

        bool exists(const string& normalized_url) override;
        optional<URLState> get(const string& normalized_url) override;
        void put(const URLState& state) override;
        void markFetched(const string& normalized_url, uint16_t http_status, const string& content_hash, uint64_t fetch_ts) override;
        void markFailed(const string& normalized_url, uint16_t http_status) override;

    private:
        unordered_map<string, URLState> url_store_;
        mutex mutex_;
    };
};

#endif // MEMORY_URL_STATE_STORE_H
