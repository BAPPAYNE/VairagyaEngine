#ifndef VALIDATE_H
#define VALIDATE_H

#include <cstdint>
#include <string>

#include "url/status.hpp"

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

enum class ResourceType {
    HTML,
    IMAGE,
    AUDIO,
    VIDEO,
    DOCUMENT,
    STATIC_ASSET,
    OTHER,
    TEXT_DOCUMENT,
    PDF_DOCUMENT,
    SITEMAP_XML,
    UNKNOWN
};


enum class Crawlability {
	CRAWLABLE,
	NON_CRAWLABLE
};

// Classification of URL analysis result

//bool isValidURL(const string url);
//bool isValidAbsoluteURL(const string input);
//bool isValidRelativeReference(const string input);
//bool isAllowedURL(const string input);

SchemeType extractScheme(const string& url);

Crawlability assessCrawlability(SchemeType& scheme);

URLStatus analyzeURL(string url, string* normalized = nullptr);

ResourceType classifyResourceType(const string& url);

uint16_t priorityScore(const string& url);
#endif // VALIDATE_H
