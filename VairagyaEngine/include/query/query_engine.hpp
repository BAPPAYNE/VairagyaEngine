#ifndef QUERY_ENGINE_H
#define QUERY_ENGINE_H

#include "query/index_searcher.h"
#include "query/query_processor.h"
#include "query/ranker.h"
#include "query/result_builder.h"
#include "storage/rocksdb_store.h"

#include <list>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

using namespace std;

namespace search {

    class QueryEngine {
    public:
        bool load(storage::RocksDBStore& store);
        SearchResponse search(const string& query, uint32_t page = 1, uint32_t limit = 10) const;
        size_t documentCount() const;
        bool registerClick(uint64_t doc_id);

    private:
        struct CacheEntry {
            string key;
            SearchResponse response;
        };

        QueryProcessor processor_;
        IndexSearcher index_;
        Ranker ranker_;
        ResultBuilder result_builder_;
        mutable mutex cache_mutex_;
        mutable list<CacheEntry> cache_entries_;
        mutable unordered_map<string, list<CacheEntry>::iterator> cache_index_;
        size_t cache_capacity_ = 128;

        static string cacheKey(const string& normalized_query, uint32_t page, uint32_t limit);
        void clearCache() const;
        bool getCached(const string& key, SearchResponse& response) const;
        void putCached(const string& key, const SearchResponse& response) const;
    };

}

#endif
