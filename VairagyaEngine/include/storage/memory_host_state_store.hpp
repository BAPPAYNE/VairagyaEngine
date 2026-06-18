#ifndef MEMORY_HOST_STATE_STORE_H
#define MEMORY_HOST_STATE_STORE_H

#include "storage/host_state_store.hpp"

#include <unordered_map>
#include <mutex>
#include <string>
#include <optional>
#include <cstdint>

namespace storage {

    class MemoryHostStateStore : public HostStateStore {
    public:
        MemoryHostStateStore() = default;
        ~MemoryHostStateStore() override = default;

        // Get current known state for a host
        optional<HostState>
            get(const string& hostname) override;
        // Insert or overwrite full host state
        void put(const HostState& state) override;
        // Enforce crawl-delay / politeness
        bool canFetchNow(const string& hostname, uint64_t now_ts) override;
        // Update last request timestamp after fetch
        void updateLastRequestTs(const string& hostname, uint64_t ts) override;

    private:
        unordered_map<string, HostState> host_store_;
        mutex mutex_;
    };

} // namespace storage

#endif // MEMORY_HOST_STATE_STORE_H
