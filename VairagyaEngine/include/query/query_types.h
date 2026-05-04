#ifndef QUERY_TYPES_H
#define QUERY_TYPES_H

#include <cstdint>
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
        string snippet;
        double score = 0.0;
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
