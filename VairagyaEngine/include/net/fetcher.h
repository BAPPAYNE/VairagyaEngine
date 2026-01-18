#ifndef FETCHER_H
#define FETCHER_H

#include <string>
#include <cctype>

namespace net {
	enum class FetchStatus {
		SUCCESS,
		FAILED,
		TIMEOUT,
		NOT_FOUND,
		UNAUTHORIZED,
		FORBIDDEN,
		SERVER_ERROR,
		UNKNOWN_ERROR
	};

	struct FetchResult {
		FetchStatus status;
		std::string content;
		uint16_t http_code;
	};

	FetchResult fetch(const std::string& url, int timeout_ms = 5000);
}

#endif // FETCHER_H
