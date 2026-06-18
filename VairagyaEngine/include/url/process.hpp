#ifndef PROCESS_H
#define PROCESS_H

#include "url/status.hpp"
#include "url/validate.hpp"

#include <string>
#include <cctype>
#include <cstdint>
#include <optional>
#include <vector>

using namespace std;

constexpr int MAX_RETRY_COUNT = 3;
constexpr int RETRY_PRIORITY_PENALTY = 10;



struct ProcessedURL {
    string original = "";
    string normalized = "";
    URLStatus status = URLStatus::INVALID_URL;
    uint16_t priority = 0;
    SchemeType scheme = SchemeType::NONE;
	Crawlability crawlability = Crawlability::NON_CRAWLABLE;
	ResourceType resource_type = ResourceType::UNKNOWN;
};


ProcessedURL processURL(const string& url);

optional<string> resolveRelativeURL(const string& url, const string& base_url);

string reverseHost(const string& url);

#endif // PROCESS_H
