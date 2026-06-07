#ifndef RESPONSE_CLASS_H
#define RESPONSE_CLASS_H

namespace net {
	enum class ResponseClass {
		OK, // 2xx
		REDIRECT, // 3xx
		CLIENT_ERROR, // 4xx (except 429 Too Many Requests)
		SERVER_ERROR, // 5xx (Internal server error)
		NETWORK_ERROR // No HTTP code / timeout
	};
} // namespace net

#endif // RESPONSE_CLASS_H
