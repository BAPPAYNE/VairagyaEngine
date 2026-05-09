#include "query/query_engine.h"

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <vector>

using namespace std;

namespace search {

    namespace {
        size_t editDistanceAtMostTwo(const string& left, const string& right) {
            if (left == right) {
                return 0;
            }
            const size_t size_delta = left.size() > right.size()
                ? left.size() - right.size()
                : right.size() - left.size();
            if (size_delta > 2) {
                return numeric_limits<size_t>::max();
            }

            const string& a = left.size() <= right.size() ? left : right;
            const string& b = left.size() <= right.size() ? right : left;

            vector<size_t> prev(b.size() + 1);
            vector<size_t> curr(b.size() + 1);
            for (size_t j = 0; j <= b.size(); ++j) {
                prev[j] = j;
            }

            for (size_t i = 1; i <= a.size(); ++i) {
                curr[0] = i;
                size_t row_min = curr[0];
                for (size_t j = 1; j <= b.size(); ++j) {
                    const size_t substitution = prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
                    curr[j] = min({prev[j] + 1, curr[j - 1] + 1, substitution});
                    row_min = min(row_min, curr[j]);
                }
                if (row_min > 2) {
                    return numeric_limits<size_t>::max();
                }
                swap(prev, curr);
            }

            return prev[b.size()];
        }

        vector<string> expandWithFuzzyTokens(const vector<string>& tokens, const IndexSearcher& index) {
            vector<string> expanded = tokens;
            unordered_set<string> seen(tokens.begin(), tokens.end());
            const auto& vocabulary = index.vocabulary();

            for (const auto& token : tokens) {
                if (token.size() < 4 || index.postingsFromToken(token)) {
                    continue;
                }

                vector<pair<size_t, string>> matches;
                for (const auto& candidate : vocabulary) {
                    if (candidate.empty() || candidate.front() != token.front()) {
                        continue;
                    }
                    const size_t distance = editDistanceAtMostTwo(token, candidate);
                    if (distance <= 2) {
                        matches.emplace_back(distance, candidate);
                    }
                }

                sort(matches.begin(), matches.end());
                for (size_t i = 0; i < min<size_t>(3, matches.size()); ++i) {
                    if (seen.insert(matches[i].second).second) {
                        expanded.push_back(matches[i].second);
                    }
                }
            }

            return expanded;
        }
    }

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

        auto effective_query = processed;
        auto candidates = index_.retrieve(effective_query.tokens);
        if (candidates.empty()) {
            effective_query.tokens = expandWithFuzzyTokens(effective_query.tokens, index_);
            candidates = index_.retrieve(effective_query.tokens);
        }

        auto ranked = ranker_.rank(effective_query, candidates, index_);
        auto response = result_builder_.build(effective_query, ranked, index_, safe_page, safe_limit);
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
