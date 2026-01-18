#include "storage/memory_content_state_store.h"

namespace storage {
	bool MemoryContentStateStore::seen(const std::string& content_hash) {
		std::lock_guard<std::mutex> lock(mutex_); // Ensure thread safety
		return content_store_.find(content_hash) != content_store_.end(); // Check existence
	}
	void MemoryContentStateStore::record(const std::string& content_hash, const std::string& url, uint64_t ts) {
		std::lock_guard<std::mutex> lock(mutex_); // Ensure thread safety
		auto it = content_store_.find(content_hash);
		if (it != content_store_.end()) {
			// Update existing ContentState
			it->second.seen_count += 1;
		}
		else {
			// Create new ContentState
			ContentState state;
			state.content_hash = content_hash;
			state.first_seen_url = url;
			state.first_seen_ts = ts;
			state.seen_count = 1;
			content_store_[content_hash] = state;
		}
	}
};