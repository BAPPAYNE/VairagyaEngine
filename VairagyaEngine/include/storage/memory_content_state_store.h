#ifndef MEMORY_CONTENT_STATE_STORE_H
#define MEMORY_CONTENT_STATE_STORE_H

#include "storage/content_state_store.h"
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include <string>

using namespace std;
using namespace storage;

namespace storage{
	class MemoryContentStateStore : public ContentStateStore {
	public:
		MemoryContentStateStore() = default;
		~MemoryContentStateStore() override = default;
		bool seen(const std::string& content_hash) override;
		void record(const std::string& content_hash, const std::string& url, uint64_t ts) override;
	private:
		std::unordered_map<std::string, ContentState> content_store_;
		std::mutex mutex_;
	};
};

#endif // MEMORY_CONTENT_STATE_STORE_H