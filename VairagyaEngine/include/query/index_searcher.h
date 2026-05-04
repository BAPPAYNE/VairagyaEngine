#ifndef INDEX_SEARCHER_H
#define INDEX_SEARCHER_H

#include "query/query_processor.h"
#include "storage/rocksdb_store.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;

namespace search {

    using TermId = uint32_t;

    struct IndexedDocument {
        uint64_t doc_id = 0;
        string title;
        string url;
        string snippet_source;
        string content_hash;
        float quality_score = 0.0f;
        time_t content_last_changed_time = 0;
        time_t last_fetched_time = 0;
        uint64_t click_count = 0;
        uint32_t document_length = 0;
    };

    struct Posting {
        uint64_t doc_id;
        uint32_t term_frequency;
        uint32_t title_frequency;
        uint32_t url_frequency;
    };

    class IndexSearcher {
    public:
        bool load(storage::RocksDBStore& store);

        // retrieval using processed tokens
        vector<uint64_t> retrieve(const vector<string>& tokens) const;

        const IndexedDocument* document(uint64_t doc_id) const;
        bool registerClick(uint64_t doc_id);

        const vector<Posting>* postings(TermId term_id) const;

        size_t documentCount() const;
        double averageDocumentLength() const;
        const vector<Posting>* postingsFromToken(const string& token) const;

    private:
        QueryProcessor processor_;

        // term dictionary
        unordered_map<string, TermId> term_to_id_;
        vector<string> id_to_term_;

        // inverted index using term IDs
        unordered_map<TermId, vector<Posting>> inverted_index_;

        // document store (dense access assumed)
        vector<IndexedDocument> documents_;

        double average_document_length_ = 0.0;
        size_t searchable_document_count_ = 0;

        TermId getOrCreateTermId(const string& term);

    };

}

#endif
