#include "query/query_engine.h"

#include <algorithm>

using namespace std;

namespace search {

    bool QueryEngine::load(storage::RocksDBStore& store) {
        return index_.load(store);
    }

    SearchResponse QueryEngine::search(const string& input, uint32_t page, uint32_t limit) const {
        auto processed = processor_.process(input);
        const uint32_t safe_page = max<uint32_t>(1, page);
        const uint32_t safe_limit = clamp<uint32_t>(limit, 1, 100);
        const string key = cacheKey(processed.normalized, safe_page, safe_limit);

        SearchResponse cached;
        if (getCached(key, cached)) {
            return cached;
        }

        if (processed.tokens.empty()) {
            SearchResponse empty;
            empty.query = input;
            empty.page = safe_page;
            empty.limit = safe_limit;
            putCached(key, empty);
            return empty;
        }

        // auto candidates = index_.retrieve(processed.tokens);
        // auto ranked = ranker_.rank(processed, index_.retrieve(processed.tokens), index_);
        auto response = result_builder_.build(processed, ranker_.rank(processed, index_.retrieve(processed.tokens), index_), index_, safe_page, safe_limit); // THis is (processed, ranked, index_, safe_page, safe_limit)
        putCached(key, response);
        return response;
    }

    size_t QueryEngine::documentCount() const {
        return index_.documentCount();
    }

    bool QueryEngine::registerClick(uint64_t doc_id) {
        if (!index_.registerClick(doc_id)) {
            return false;
        }
        clearCache();
        return true;
    }

    string QueryEngine::cacheKey(const string& normalized_query, uint32_t page, uint32_t limit) {
        return normalized_query + "|" + to_string(page) + "|" + to_string(limit);
    }

    bool QueryEngine::getCached(const string& key, SearchResponse& response) const {
        lock_guard<mutex> lock(cache_mutex_);
        auto it = cache_index_.find(key);
        if (it == cache_index_.end()) {
            return false;
        }

        cache_entries_.splice(cache_entries_.begin(), cache_entries_, it->second);
        response = it->second->response;
        return true;
    }

    void QueryEngine::putCached(const string& key, const SearchResponse& response) const {
        lock_guard<mutex> lock(cache_mutex_);

        auto it = cache_index_.find(key);
        if (it != cache_index_.end()) {
            it->second->response = response;
            cache_entries_.splice(cache_entries_.begin(), cache_entries_, it->second);
            return;
        }

        cache_entries_.push_front({key, response});
        cache_index_[key] = cache_entries_.begin();

        if (cache_entries_.size() > cache_capacity_) {
            auto last = prev(cache_entries_.end());
            cache_index_.erase(last->key);
            cache_entries_.pop_back();
        }
    }

    void QueryEngine::clearCache() const {
        lock_guard<mutex> lock(cache_mutex_);
        cache_entries_.clear();
        cache_index_.clear();
    }

}
