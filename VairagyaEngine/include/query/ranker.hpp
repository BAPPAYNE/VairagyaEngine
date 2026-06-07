#ifndef QUERY_RANKER_H
#define QUERY_RANKER_H

#include "query/index_searcher.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;

namespace search {

    struct RankedDocument {
        uint64_t doc_id = 0;
        double score = 0.0;
    };

    class Ranker {
    public:
        vector<RankedDocument> rank(
            const ProcessedQuery& query,
            const vector<uint64_t>& candidates,
            const IndexSearcher& index
        ) const;

    private:
        static double bm25(
            uint32_t term_frequency,
            uint32_t document_frequency,
            uint32_t document_length,
            uint32_t document_count,
            double average_document_length
        );
    };

}

#endif
