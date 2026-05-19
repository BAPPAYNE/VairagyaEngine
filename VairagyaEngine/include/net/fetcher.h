#ifndef FETCHER_H
#define FETCHER_H

#include <string>
#include <cctype>
#include <cstdint>

using namespace std;

namespace net {
	enum class FetchStatus {
		SUCCESS,
		FAILED,
		TIMEOUT,
		NOT_FOUND,
		UNAUTHORIZED,
		FORBIDDEN,
		ROBOTS_DISALLOWED,
		SERVER_ERROR,
		UNKNOWN_ERROR
	};

	struct FetchResult {
		FetchStatus status;
		string content;
		uint16_t http_code;
		long long fetch_time_ms;
        string content_type;
	};

	FetchResult fetch(const string& url, int timeout_ms = 5000);
}

#endif // FETCHER_H
