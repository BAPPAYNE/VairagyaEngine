#include "net/fetcher.h"

namespace net {
	FetchResult fetch(const std::string& url, int timeout_ms) {
		// Placeholder implementation
		FetchResult result;
		result.status = FetchStatus::SUCCESS;
		result.content = "<html>Mock Content</html>";
		result.http_code = 200;
		return result;
	}

};