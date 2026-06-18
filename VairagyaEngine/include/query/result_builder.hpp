#ifndef QUERY_RESULT_BUILDER_H
#define QUERY_RESULT_BUILDER_H

#include "query/index_searcher.hpp"
#include "query/ranker.hpp"
#include "query/snippet_generator.hpp"

#include <cstdint>
#include <vector>

using namespace std;

namespace search {

    class ResultBuilder {
    public:
        SearchResponse build(
            const ProcessedQuery& query,
            const vector<RankedDocument>& ranked,
            const IndexSearcher& index,
            uint32_t page,
            uint32_t limit
        ) const;

    private:
        SnippetGenerator snippet_generator_;
    };

}

#endif
