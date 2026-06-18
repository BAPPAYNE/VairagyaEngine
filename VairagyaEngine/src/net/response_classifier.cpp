#include "net/response_classifier.hpp"

namespace net {
	ResponseClass classify(const FetchResult& result) {
		uint16_t http_code = result.http_code;
		if (http_code == 0) {
			return ResponseClass::NETWORK_ERROR;
		}
		else if (http_code >= 200 && http_code < 300) {
			return ResponseClass::OK;
		}
		else if (http_code >= 300 && http_code < 400) {
			return ResponseClass::REDIRECT;
		}
		else if (http_code >= 400 && http_code < 500) {
			return ResponseClass::CLIENT_ERROR;
		}
		else if (http_code >= 500) {
			return ResponseClass::SERVER_ERROR;
		}

		return ResponseClass::NETWORK_ERROR;
	}
}