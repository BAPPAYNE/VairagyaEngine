#ifndef HOST_STATE_STORE_H
#define HOST_STATE_STORE_H

#include "storage/host_state.hpp"

#include <cstdint>
#include <string>
#include <optional>

using namespace std;

namespace storage {
	class HostStateStore {
	public:
		virtual ~HostStateStore() = default;
		// Retrieve the state of a host
		virtual optional<HostState> get(const string& hostname) = 0;
		// Store or update the state of a host
		virtual void put(const HostState& state) = 0;
		// Determine if a fetch can be made now based on crawl delay and last request time
		virtual bool canFetchNow(const string& hostname, uint64_t now_ts) = 0;
		// Update the last request timestamp for a host after a fetch
		virtual void updateLastRequestTs(const string& hostname, uint64_t ts) = 0;
	};
}; // namespace storage

#endif // HOST_STATE_STORE_H
