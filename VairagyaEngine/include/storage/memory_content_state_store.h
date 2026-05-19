#ifndef MEMORY_CONTENT_STATE_STORE_H
#define MEMORY_CONTENT_STATE_STORE_H

#include "storage/content_state_store.h"
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include <string>

using namespace std;
using namespace storage;

using namespace std;

namespace storage{
	class MemoryContentStateStore : public ContentStateStore {
	public:
		MemoryContentStateStore() = default;
		~MemoryContentStateStore() override = default;
		bool seen(const string& content_hash) override;
		void record(const string& content_hash, const string& url, uint64_t ts) override;
	private:
		unordered_map<string, ContentState> content_store_;
		mutex mutex_;
	};
};

#endif // MEMORY_CONTENT_STATE_STORE_H