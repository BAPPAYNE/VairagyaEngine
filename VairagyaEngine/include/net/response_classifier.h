#ifndef RESPONSE_CLASSIFIER_H
#define RESPONSE_CLASSIFIER_H

#include "net/fetcher.h"
#include "net/response_class.h"

namespace net {

	ResponseClass classify(const FetchResult& result);
}

#endif // RESPONSE_CLASSIFIER_H
