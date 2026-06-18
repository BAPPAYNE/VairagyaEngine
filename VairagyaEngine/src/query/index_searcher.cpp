#include "query/index_searcher.hpp"

#include <algorithm>
#include <tsl/robin_map.h>

using namespace std;

namespace search {

    namespace {

        constexpr size_t MAX_SNIPPET_SOURCE_CHARS = 4096;

        struct TermCounts {
            uint32_t body = 0;
            uint32_t title = 0;
            uint32_t description = 0;
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

    } // namespace

    TermId IndexSearcher::getOrCreateTermId(const string& term) {

        auto [it, inserted] =
            term_to_id_.try_emplace(
                term,
                static_cast<TermId>(id_to_term_.size())
            );

        if (inserted) {
            id_to_term_.push_back(term);
        }

        return it->second;
    }

    bool IndexSearcher::load(storage::RocksDBStore& store) {

        // TERM ID -> POSTINGS
        // vector index == TermId
        vector<vector<Posting>> inverted;

        vector<IndexedDocument> docs;

        const auto click_counts = store.loadClickCounts();

        uint64_t total_len = 0;

        size_t searchable_count = 0;

        // Optional:
        // inverted.reserve(estimated_vocab_size);
        // docs.reserve(estimated_doc_count);

        store.forEachSearchDocument([&](storage::SearchDocumentRecord&& record) {

            // FILTER BAD DOCUMENTS
            if (record.doc_core.doc_id == 0 ||
                record.doc_core.normalized_url.empty() ||
                record.fetch_meta.fetch_status_code != 200 ||
                record.parsed_content.clean_text.empty() ||
                record.control_flags.noindex ||
                record.content_meta.is_duplicate ||
                record.quality_signals.spam_score > 0.8f) {

                return;
            }

            const uint64_t doc_id = record.doc_core.doc_id;

            // ENSURE DOC VECTOR SIZE
            if (doc_id >= docs.size()) {
                docs.resize(doc_id + 1);
            }

            // TOKENIZE SEPARATELY
            const auto body_tokens =
                processor_.process(
                    record.parsed_content.clean_text
                ).tokens;

            const auto title_tokens =
                processor_.process(
                    record.parsed_content.title
                ).tokens;

            const auto description_tokens =
                processor_.process(
                    record.parsed_content.meta_description
                ).tokens;

            const auto url_tokens =
                processor_.process(
                    record.doc_core.normalized_url
                ).tokens;

            // LOCAL TERM COUNTS
            tsl::robin_map<TermId, TermCounts> local_counts;

            local_counts.reserve(
                max<size_t>(64, body_tokens.size() / 2)
            );

            // BODY
            for (const auto& token : body_tokens) {

                TermId tid = getOrCreateTermId(token);

                local_counts[tid].body++;
            }

            // TITLE
            for (const auto& token : title_tokens) {

                TermId tid = getOrCreateTermId(token);

                local_counts[tid].title++;
            }

            // DESCRIPTION
            for (const auto& token : description_tokens) {

                TermId tid = getOrCreateTermId(token);

                local_counts[tid].description++;
            }

            // URL
            for (const auto& token : url_tokens) {

                TermId tid = getOrCreateTermId(token);

                local_counts[tid].url++;
            }

            // INSERT POSTINGS
            for (const auto& [tid, counts] : local_counts) {

                // ENSURE TERM VECTOR SIZE
                if (tid >= inverted.size()) {
                    inverted.resize(tid + 1);
                }

                inverted[tid].push_back({
                    doc_id,
                    counts.body,
                    counts.title,
                    counts.description,
                    counts.url
                    });
            }

            // BUILD DOCUMENT
            IndexedDocument doc;

            doc.doc_id = doc_id;

            doc.title =
                record.parsed_content.title.empty()
                ? record.doc_core.normalized_url
                : record.parsed_content.title;

            doc.url =
                record.doc_core.normalized_url;

            doc.display_url =
                record.presentation.display_url.empty()
                ? record.doc_core.normalized_url
                : record.presentation.display_url;

            doc.favicon_url =
                record.presentation.favicon_url;

            doc.language =
                record.doc_core.language_code;

            doc.snippet_source =
                cappedSnippet(record);

            doc.content_hash =
                record.content_meta.content_hash;

            doc.quality_score =
                record.quality_signals.quality_score;

            doc.spam_score =
                record.quality_signals.spam_score;

            doc.pagerank_score =
                record.link_data.pagerank_score;

            doc.inbound_links_count =
                record.link_data.inbound_links_count;

            doc.outbound_links_count =
                record.link_data.outbound_links_count;

            doc.crawl_depth =
                record.fetch_meta.crawl_depth;

            doc.content_last_changed_time =
                record.quality_signals.content_last_changed_time;

            doc.last_fetched_time =
                record.fetch_meta.last_fetched_time;

            if (auto click_it = click_counts.find(doc_id);
                click_it != click_counts.end()) {

                doc.click_count = click_it->second;
            }

            doc.document_length =
                max<size_t>(1, body_tokens.size());

            docs[doc_id] = move(doc);

            total_len += docs[doc_id].document_length;

            searchable_count++;
            });

        // NO SORT REQUIRED
        // documents are indexed in increasing doc_id order,
        // postings already naturally sorted.

        inverted_index_ = move(inverted);

        documents_ = move(docs);

        searchable_document_count_ = searchable_count;

        average_document_length_ =
            searchable_document_count_ == 0
            ? 1.0
            : static_cast<double>(total_len) /
            searchable_document_count_;

        return true;
    }

    const vector<Posting>* IndexSearcher::postingsFromToken(
        const string& token
    ) const {

        auto it = term_to_id_.find(token);

        if (it == term_to_id_.end()) {
            return nullptr;
        }

        TermId tid = it->second;

        if (tid >= inverted_index_.size()) {
            return nullptr;
        }

        return &inverted_index_[tid];
    }

    vector<uint64_t> IndexSearcher::retrieve(
        const vector<string>& tokens
    ) const {

        vector<const vector<Posting>*> lists;

        // GET POSTING LISTS
        for (const auto& token : tokens) {

            auto p = postingsFromToken(token);

            if (p && !p->empty()) {
                lists.push_back(p);
            }
        }

        if (lists.empty()) {
            return {};
        }

        // SMALLEST LIST FIRST
        sort(
            lists.begin(),
            lists.end(),
            [](auto a, auto b) {
                return a->size() < b->size();
            }
        );

        // FAST MATCH COUNTS
        vector<uint16_t> match_counts(
            documents_.size(),
            0
        );

        vector<uint64_t> touched_docs;

        // COUNT MATCHES
        for (const auto* list : lists) {

            for (const auto& posting : *list) {

                if (match_counts[posting.doc_id] == 0) {
                    touched_docs.push_back(posting.doc_id);
                }

                ++match_counts[posting.doc_id];
            }
        }

        const uint32_t required_matches =
            lists.size() <= 2
            ? 1U
            : static_cast<uint32_t>(
                (lists.size() + 1) / 2
                );

        vector<uint64_t> result;

        result.reserve(touched_docs.size());

        for (uint64_t doc_id : touched_docs) {

            if (match_counts[doc_id] >= required_matches) {
                result.push_back(doc_id);
            }

            // RESET FOR NEXT QUERY
            match_counts[doc_id] = 0;
        }

        sort(result.begin(), result.end());

        return result;
    }

    const IndexedDocument* IndexSearcher::document(
        uint64_t doc_id
    ) const {

        if (doc_id >= documents_.size()) {
            return nullptr;
        }

        if (documents_[doc_id].doc_id == 0) {
            return nullptr;
        }

        return &documents_[doc_id];
    }

    bool IndexSearcher::registerClick(uint64_t doc_id) {

        if (doc_id >= documents_.size() ||
            documents_[doc_id].doc_id == 0) {

            return false;
        }

        ++documents_[doc_id].click_count;

        return true;
    }

    const vector<Posting>* IndexSearcher::postings(
        TermId term_id
    ) const {

        if (term_id >= inverted_index_.size()) {
            return nullptr;
        }

        return &inverted_index_[term_id];
    }

    size_t IndexSearcher::documentCount() const {
        return searchable_document_count_;
    }

    double IndexSearcher::averageDocumentLength() const {
        return average_document_length_;
    }

    const vector<string>& IndexSearcher::vocabulary() const {
        return id_to_term_;
    }

} // namespace search