#ifndef PROCESS_H
#define PROCESS_H

#include "url/status.h"
#include "url/validate.h"

#include <string>
#include <cctype>
#include <optional>
#include <vector>

using namespace std;

constexpr int MAX_RETRY_COUNT = 3;
constexpr int RETRY_PRIORITY_PENALTY = 10;



struct ProcessedURL {
    string original;
    string normalized;
    URLStatus status;
    int priority;
    SchemeType scheme;
	Crawlability crawlability;
};


ProcessedURL processURL(const string& url);

int priorityScore(const string& url);

optional<string> resolveRelativeURL(const string& url, const string& base_url);

string reverseHost(const string& url);

#endif // PROCESS_H