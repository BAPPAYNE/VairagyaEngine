#ifndef QUERY_TYPES_H
#define QUERY_TYPES_H

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

using namespace std;

namespace search {

    struct ProcessedQuery {
        string original;
        string normalized;
        vector<string> tokens;
    };

    struct SearchResult {
        uint64_t doc_id = 0;
        string title;
        string url;
        string display_url;
        string favicon_url;
        string language;
        string snippet;
        double score = 0.0;
        time_t last_fetched_time = 0;
        float quality_score = 0.0f;
    };

    struct SearchResponse {
        string query;
        uint32_t page = 1;
        uint32_t limit = 10;
        uint64_t total = 0;
        vector<SearchResult> results;
    };

}

#endif
