#include "storage/memory_host_state_store.hpp"

#include <optional>
#include <mutex>

using namespace std;

namespace storage {
	optional<HostState> MemoryHostStateStore::get(const string& hostname) {
		lock_guard<mutex> lock(mutex_);
		auto status = host_store_.find(hostname);
		if (status != host_store_.end()) {
			return status->second;
		}
		return nullopt;
	}

	void MemoryHostStateStore::put(const HostState& state) {
		lock_guard<mutex> lock(mutex_);
		host_store_[state.host] = state;
	}

	bool MemoryHostStateStore::canFetchNow(const string& hostname, uint64_t now_ts) {
		lock_guard<mutex> lock(mutex_);
		auto state = host_store_.find(hostname);
		if (state == host_store_.end()) {
			// No known state, allow fetch
			return true;
		}
		uint64_t next_allowed_ts = state->second.last_request_ts + state->second.crawl_delay_ms;
		return now_ts >= next_allowed_ts;
	}

	void MemoryHostStateStore::updateLastRequestTs(const string& hostname, uint64_t ts) {
		lock_guard<mutex> lock(mutex_);
		auto& state = host_store_[hostname];
		state.host = hostname;
		state.last_request_ts = ts;
	}
};