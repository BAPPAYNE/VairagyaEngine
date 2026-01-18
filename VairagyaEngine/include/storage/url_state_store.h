#ifndef URL_STATE_STORE_H
#define URL_STATE_STORE_H

#include "storage/url_state.h"

#include <optional>
#include <string>
#include <cstdint>

using namespace std;

namespace storage {
	class URLStateStore {
	public:
		virtual ~URLStateStore() = default;

		virtual bool exists(const string& normalized_url) = 0; // Check if URLState exists

		virtual optional<URLState> get(const string& normalized_url) = 0;

		virtual void put(const URLState& state) = 0;

		virtual void markFetched(const string& normalized_url, uint16_t http_status, const std::string& content_hash, uint64_t fetch_ts) = 0;

		virtual void markFailed(const string& normalized_url, uint16_t http_status) = 0;

	};
};

#endif // URL_STATE_STORE_H
