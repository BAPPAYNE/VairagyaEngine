#include "query/result_builder.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

using namespace std;

namespace search {

    namespace {
        bool looksLikeCodeNoise(const string& text) {
            if (text.empty()) {
                return true;
            }

            static QueryProcessor processor;
            const string lowered = processor.normalize(text);

            if (lowered.find("function ") != string::npos ||
                lowered.find("typeof ") != string::npos ||
                lowered.find("window ") != string::npos ||
                lowered.find("document ") != string::npos ||
                lowered.find("media ") != string::npos ||
                lowered.find("ezcookie") != string::npos ||
                lowered.find("sideavailable") != string::npos ||
                lowered.find("siderail") != string::npos) {
                return true;
            }

            size_t braces = 0;
            size_t semicolons = 0;

            for (char ch : text) {
                if (ch == '{' || ch == '}') braces++;
                if (ch == ';') semicolons++;
            }

            return braces + semicolons > text.size() / 20;
        }

        string buildSnippetSource(const IndexedDocument& document) {
            if (!looksLikeCodeNoise(document.snippet_source)) {
                return document.snippet_source;
            }
            if (!document.title.empty()) {
                return document.title;
            }
            return document.url;
        }
    }

    SearchResponse ResultBuilder::build(
        const ProcessedQuery& query,
        const vector<RankedDocument>& ranked,
        const IndexSearcher& index,
        uint32_t page,
        uint32_t limit
    ) const {
        SearchResponse response;

        response.query = query.original;
        response.page = std::max<uint32_t>(1, page);
        response.limit = std::clamp<uint32_t>(limit, 1, 100);

        vector<RankedDocument> deduped;
        deduped.reserve(ranked.size());

        unordered_set<string> content_hashes;

        for (const auto& item : ranked) {
            const auto* document = index.document(item.doc_id);
            if (!document) continue;

            const string& content_hash = document->content_hash;
            if (!content_hash.empty() &&
                !content_hashes.insert(content_hash).second) {
                continue;
            }

            deduped.push_back(item);
        }

        response.total = static_cast<uint64_t>(deduped.size());

        const size_t start =
            static_cast<size_t>(response.page - 1) * response.limit;

        if (start >= deduped.size()) {
            return response;
        }

        const size_t end =
            std::min(deduped.size(), start + response.limit);

        for (size_t i = start; i < end; ++i) {
            const auto* document = index.document(deduped[i].doc_id);
            if (!document) continue;

            SearchResult result;
            result.doc_id = document->doc_id;
            result.title = document->title;
            result.url = document->url;
            result.display_url = document->display_url;
            result.favicon_url = document->favicon_url;
            result.language = document->language;
            result.snippet = snippet_generator_.generate(
                buildSnippetSource(*document),
                query.tokens
            );
            result.score = deduped[i].score;
            result.last_fetched_time = document->last_fetched_time;
            result.quality_score = document->quality_score;

            response.results.push_back(result);
        }

        return response;
    }

}
