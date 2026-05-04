#include "query/ranker.h"
#include "query/query_processor.h"


#include <algorithm>
#include <cmath>
#include <ctime>
#include <unordered_map>

using namespace std;

namespace search {

    namespace {
        bool startsWith(const string& value, const string& prefix) {
            return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
        }
    }

    vector<RankedDocument> Ranker::rank(
        const ProcessedQuery& query,
        const vector<uint64_t>& candidates,
        const IndexSearcher& index
    ) const {
        unordered_map<uint64_t, double> scores;
        static QueryProcessor processor;
        const time_t now = time(nullptr);

        const auto document_count = static_cast<uint32_t>(max<size_t>(1, index.documentCount()));
        const double average_length = max(1.0, index.averageDocumentLength());

        for (const auto& token : query.tokens) {
            const vector<Posting>* token_postings = index.postingsFromToken(token);
            if (!token_postings) {
                continue;
            }

            const auto document_frequency = static_cast<uint32_t>(token_postings->size());
            for (const auto& posting : *token_postings) {
                const auto* document = index.document(posting.doc_id);
                if (!document) {
                    continue;
                }

                double score = bm25(
                    posting.term_frequency,
                    document_frequency,
                    document->document_length,
                    document_count,
                    average_length
                );

                if (posting.title_frequency > 0) {
                    score += 2.0 + (0.5 * posting.title_frequency);
                }
                if (posting.url_frequency > 0) {
                    score += 0.75 + (0.25 * posting.url_frequency);
                }

                scores[posting.doc_id] += score;
            }
        }

        for (const auto doc_id : candidates) {
            const auto* document = index.document(doc_id);
            if (!document) {
                continue;
            }

            auto& score = scores[doc_id];
            if (!query.normalized.empty()) {
                const string title = processor.normalize(document->title);
                const string body = processor.normalize(document->snippet_source);
                const string url = processor.normalize(document->url);

                if (title == query.normalized) {
                    score += 8.0;
                } else if (startsWith(title, query.normalized + " ")) {
                    score += 4.0;
                } else if (title.find(query.normalized) != string::npos) {
                    score += 3.0;
                } else if (body.find(query.normalized) != string::npos) {
                    score += 1.5;
                }

                if (url.find(query.normalized) != string::npos) {
                    score += 1.0;
                }

                if (query.tokens.size() == 1 && title.find("about " + query.normalized) != string::npos) {
                    score += 2.5;
                }
            }

            const time_t freshness_time = document->content_last_changed_time > 0
                ? document->content_last_changed_time
                : document->last_fetched_time;
            if (freshness_time > 0 && now > freshness_time) {
                const double days_old = static_cast<double>(now - freshness_time) / 86400.0;
                score += 3.0 / (1.0 + (days_old / 14.0));
            }

            score += log1p(static_cast<double>(document->click_count)) * 0.75;
            score += max(0.0f, document->quality_score);
        }

        vector<RankedDocument> ranked;
        ranked.reserve(scores.size());
        for (const auto& [doc_id, score] : scores) {
            if (score > 0.01) {
                ranked.push_back({doc_id, score});
            }
        }

        sort(ranked.begin(), ranked.end(), [](const RankedDocument& left, const RankedDocument& right) {
            return left.score > right.score;
        });

        return ranked;
    }

    double Ranker::bm25(
        uint32_t term_frequency,
        uint32_t document_frequency,
        uint32_t document_length,
        uint32_t document_count,
        double average_document_length
    ) {
        if (term_frequency == 0 || document_frequency == 0) {
            return 0.0;
        }

        constexpr double k1 = 1.5;
        constexpr double b = 0.75;
        const double idf = log(1.0 + ((document_count - document_frequency + 0.5) / (document_frequency + 0.5)));
        const double tf = static_cast<double>(term_frequency);
        const double length = static_cast<double>(max<uint32_t>(1, document_length));
        const double denominator = tf + k1 * (1.0 - b + b * (length / average_document_length));

        return idf * ((tf * (k1 + 1.0)) / denominator);
    }

}
