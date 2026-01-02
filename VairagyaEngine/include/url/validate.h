#ifndef VALIDATE_H
#define VALIDATE_H

#include <string>

#include "url/status.h"

using namespace std;

enum class SchemeType {
    HTTP,
    HTTPS,
    FTP,
    MAILTO,
    TEL,
    SCP,
    MQTT,
    MQTTS,
    UNKNOWN,
    NONE
};


enum class Crawlability {
	CRAWLABLE,
	NON_CRAWLABLE
};

// Classification of URL analysis result

//bool isValidURL(const std::string url);
//bool isValidAbsoluteURL(const std::string input);
//bool isValidRelativeReference(const std::string input);
//bool isAllowedURL(const std::string input);

SchemeType extractScheme(const string& url);

Crawlability assessCrawlability(SchemeType& scheme);

URLStatus analyzeURL(string url, string* normalized = nullptr);
#endif // VALIDATE_H