#ifndef PROCESS_H
#define PROCESS_H

#include <string>
#include <cctype>
#include "url/status.h"
#include "url/validate.h"

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

#endif // PROCESS_H