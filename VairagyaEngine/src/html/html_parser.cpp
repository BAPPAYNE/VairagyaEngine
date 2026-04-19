#include "html/html_parser.h"
#include "url/process.h"

#include <iostream>
#include <string>
#include <vector>
#include <regex>

using namespace std;

static regex link_regex(R"((?:href|src|loc)\s*=\s*(?:['"]([^'"]*)['"]|([^\s>]+))|<(?:link|loc)\b[^>]*>([^<]+)</(?:link|loc)>)", regex::icase | regex::optimize);

vector<string> extractLinks(const string& content) {
    vector<string> links;

    auto begin = sregex_iterator(content.begin(), content.end(), link_regex);
    auto end = sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        smatch m = *it;
        string raw_url;
        if (m[1].matched) {
            raw_url = m[1].str();
        } else if (m[2].matched) {
            raw_url = m[2].str();
        } else if (m[3].matched) {
            raw_url = m[3].str();
        }
        
        if (!raw_url.empty()) {
            // Basic cleaning: remove surrounding quotes if any
            if (raw_url.front() == '\'' || raw_url.front() == '"') raw_url.erase(0, 1);
            if (!raw_url.empty() && (raw_url.back() == '\'' || raw_url.back() == '"')) raw_url.pop_back();
            
            // Trim whitespace
            raw_url.erase(0, raw_url.find_first_not_of(" \t\r\n"));
            raw_url.erase(raw_url.find_last_not_of(" \t\r\n") + 1);
            
            if (!raw_url.empty()) {
                links.emplace_back(raw_url);
            }
        }
    }

    return links;
}
