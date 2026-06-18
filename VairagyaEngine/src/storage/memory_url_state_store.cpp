#include "storage/memory_url_state_store.hpp"


using namespace std;

namespace storage {
	bool MemoryURLStateStore::exists(const string& normalized_url) {
		lock_guard<mutex> lock(mutex_); // Ensure thread safety
		return url_store_.find(normalized_url) != url_store_.end(); // Check existence
	};

	optional<URLState> MemoryURLStateStore::get(const string& normalized_url) {
		lock_guard<mutex> lock(mutex_);

		auto it = url_store_.find(normalized_url);
		if (it != url_store_.end()) {
			return it->second;
		}
		return nullopt;
	}

	void MemoryURLStateStore::put(const URLState& state) {
		lock_guard<mutex> lock(mutex_);
		url_store_[state.normalized_url] = state;
	}

	void MemoryURLStateStore::markFetched(const string& normalized_url, uint16_t http_status, const string& content_hash, uint64_t fetch_ts) {
		lock_guard<mutex> lock(mutex_);

		auto it = url_store_.find(normalized_url);
		if (it != url_store_.end()) {
			it->second.fetch_status = net::FetchStatus::SUCCESS;
			it->second.http_status = http_status;
			it->second.content_hash = content_hash;
			it->second.last_fetch_ts = fetch_ts;
			it->second.retry_count = 0; // Reset retry count on success
		}
	}

	void MemoryURLStateStore::markFailed(const string& normalized_url, uint16_t http_status) {
		lock_guard<mutex> lock(mutex_);
		auto it = url_store_.find(normalized_url);
		if (it != url_store_.end()) {
			it->second.fetch_status = net::FetchStatus::FAILED;
			it->second.http_status = http_status;
			it->second.retry_count++; // Increment retry count on failure
			it->second.last_fetch_ts = static_cast<uint64_t>(time(nullptr)); // Update last fetch timestamp
		}
	}
};