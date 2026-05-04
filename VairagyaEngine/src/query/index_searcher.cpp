#include "query/index_searcher.h"

#include <algorithm>
using namespace std;

namespace search {

    namespace {
        constexpr size_t MAX_SNIPPET_SOURCE_CHARS = 512;
        struct TermCounts {
            uint32_t body = 0;
            uint32_t title = 0;
            uint32_t url = 0;
        };

        string cappedSnippet(const storage::SearchDocumentRecord& record) {
            string src;

            if (!record.parsed_content.meta_description.empty())
                src = record.parsed_content.meta_description;
            else if (!record.parsed_content.clean_text.empty())
                src = record.parsed_content.clean_text;
            else if (!record.parsed_content.title.empty())
                src = record.parsed_content.title;
            else
                src = record.doc_core.normalized_url;

            if (src.size() > MAX_SNIPPET_SOURCE_CHARS)
                src.resize(MAX_SNIPPET_SOURCE_CHARS);

            return src;
        }
    }

    TermId IndexSearcher::getOrCreateTermId(const string& term) {
        auto it = term_to_id_.find(term);
        if (it != term_to_id_.end()) {
            return it->second;
        }

        TermId id = static_cast<TermId>(id_to_term_.size());
        term_to_id_[term] = id;
        id_to_term_.push_back(term);
        return id;
    }

    bool IndexSearcher::load(storage::RocksDBStore& store) {
        unordered_map<TermId, vector<Posting>> inverted;
        vector<IndexedDocument> docs;
        const auto click_counts = store.loadClickCounts();

        uint64_t total_len = 0;
        size_t searchable_count = 0;

        store.forEachSearchDocument([&](storage::SearchDocumentRecord&& record) {

            if (record.doc_core.doc_id == 0 ||
                record.doc_core.normalized_url.empty() ||
                record.control_flags.noindex ||
                record.content_meta.is_duplicate ||
                record.quality_signals.spam_score > 0.8f) {
                return;
            }

            auto body_tokens  = processor_.process(record.parsed_content.clean_text).tokens;
            auto title_tokens = processor_.process(record.parsed_content.title).tokens;
            auto url_tokens   = processor_.process(record.doc_core.normalized_url).tokens;

            unordered_map<string, TermCounts> term_counts;
            term_counts.reserve(body_tokens.size() + title_tokens.size() + url_tokens.size());
            for (const auto& token : body_tokens) {
                term_counts[token].body++;
            }
            for (const auto& token : title_tokens) {
                term_counts[token].title++;
            }
            for (const auto& token : url_tokens) {
                term_counts[token].url++;
            }

            uint64_t doc_id = record.doc_core.doc_id;

            // ensure vector size
            if (doc_id >= docs.size())
                docs.resize(doc_id + 1);

            for (const auto& [term, counts] : term_counts) {
                TermId tid = getOrCreateTermId(term);

                inverted[tid].push_back({
                    doc_id,
                    counts.body,
                    counts.title,
                    counts.url
                });
            }

            IndexedDocument doc;
            doc.doc_id = doc_id;
            doc.title = record.parsed_content.title.empty()
                ? record.doc_core.normalized_url
                : record.parsed_content.title;

            doc.url = record.doc_core.normalized_url;
            doc.snippet_source = cappedSnippet(record);
            doc.content_hash = record.content_meta.content_hash;
            doc.quality_score = record.quality_signals.quality_score;
            doc.content_last_changed_time = record.quality_signals.content_last_changed_time;
            doc.last_fetched_time = record.fetch_meta.last_fetched_time;
            if (auto click_it = click_counts.find(doc_id); click_it != click_counts.end())
                doc.click_count = click_it->second;
            doc.document_length = max<size_t>(1, body_tokens.size());

            docs[doc_id] = move(doc);
            total_len += docs[doc_id].document_length;
            searchable_count++;
        });

        // sort postings for fast intersection
        for (auto& [tid, plist] : inverted) {
            sort(plist.begin(), plist.end(),
                [](const Posting& a, const Posting& b) {
                    return a.doc_id < b.doc_id;
                });
        }

        inverted_index_ = move(inverted);
        documents_ = move(docs);
        searchable_document_count_ = searchable_count;

        average_document_length_ =
            searchable_document_count_ == 0 ? 1.0 :
            static_cast<double>(total_len) / searchable_document_count_;

        return true;
    }

    const vector<Posting>* IndexSearcher::postingsFromToken(const string& token) const {
        auto it = term_to_id_.find(token);
        if (it == term_to_id_.end()) return nullptr;

        auto pit = inverted_index_.find(it->second);
        return (pit == inverted_index_.end()) ? nullptr : &pit->second;
    }

    vector<uint64_t> IndexSearcher::retrieve(const vector<string>& tokens) const {
        vector<const vector<Posting>*> lists;

        for (const auto& token : tokens) {
            auto p = postingsFromToken(token);
            if (p) lists.push_back(p);
        }

        if (lists.empty()) return {};

        // sort by smallest list first (important optimization)
        sort(lists.begin(), lists.end(),
            [](auto a, auto b) {
                return a->size() < b->size();
            });

        // intersection
        vector<uint64_t> result;
        for (const auto& posting : *lists[0]) {
            uint64_t doc = posting.doc_id;
            bool match = true;

            for (size_t i = 1; i < lists.size(); ++i) {
                const auto& lst = *lists[i];
                auto found = lower_bound(lst.begin(), lst.end(), doc,
                    [](const Posting& p, uint64_t id) {
                        return p.doc_id < id;
                    });
                if (found == lst.end() || found->doc_id != doc) {
                    match = false;
                    break;
                }
            }

            if (match)
                result.push_back(doc);
        }

        return result;
    }

    const IndexedDocument* IndexSearcher::document(uint64_t doc_id) const {
        if (doc_id >= documents_.size()) return nullptr;
        if (documents_[doc_id].doc_id == 0) return nullptr;
        return &documents_[doc_id];
    }

    bool IndexSearcher::registerClick(uint64_t doc_id) {
        if (doc_id >= documents_.size() || documents_[doc_id].doc_id == 0) return false;
        ++documents_[doc_id].click_count;
        return true;
    }

    const vector<Posting>* IndexSearcher::postings(TermId term_id) const {
        auto it = inverted_index_.find(term_id);
        return (it == inverted_index_.end()) ? nullptr : &it->second;
    }

    size_t IndexSearcher::documentCount() const {
        return searchable_document_count_;
    }

    double IndexSearcher::averageDocumentLength() const {
        return average_document_length_;
    }

}
