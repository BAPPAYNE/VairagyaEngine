#ifndef CONTENT_STATE_STORE
#define CONTENT_STATE_STORE

#include "storage/content_state.hpp"

#include <string>
#include <cstdint>
#include <unordered_map>
#include <mutex>

using namespace std;

namespace storage {
	class ContentStateStore {
	public:
		virtual ~ContentStateStore() = default;

		virtual bool seen(const string& content_hash) = 0; // Check if ContentState exists

		virtual void record(const string& content_hash, const string& url, uint64_t ts) = 0; // record content state

	};
}; // namespace storage

#endif // CONTENT_STATE_STORE
