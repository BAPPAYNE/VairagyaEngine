#ifndef RESPONSE_CLASSIFIER_H
#define RESPONSE_CLASSIFIER_H

#include "net/fetcher.hpp"
#include "net/response_class.hpp"

namespace net {

	ResponseClass classify(const FetchResult& result);
}

#endif // RESPONSE_CLASSIFIER_H
