#ifndef ROCKSDB_STORE_H
#define ROCKSDB_STORE_H

#include <string>
#include <cstdint>
#include <vector>
#include <map>
#include <unordered_map>
#include <functional>
#include <memory>
#include <mutex>
#include "storage/kv_store.h"
#include "storage/db_schema.h"
#include <rocksdb/db.h>

using namespace std;

namespace storage {

    struct DuplicateRemovalStats {
        uint64_t duplicate_doc_records_removed = 0;
        uint64_t duplicate_domain_index_entries_removed = 0;
        uint64_t duplicate_pending_urls_removed = 0;
        bool next_doc_id_repaired = false;
        uint64_t next_doc_id = 0;
    };

    struct SearchDocumentRecord {
        DocCore doc_core;
        FetchMeta fetch_meta;
        ParsedContent parsed_content;
        ContentMeta content_meta;
        LinkData link_data;
        QualitySignals quality_signals;
        Presentation presentation;
        ControlFlags control_flags;
    };

    class RocksDBStore : public KVStore {
    public:
        RocksDBStore();
        ~RocksDBStore() override;

        bool open(const string& path) override;
        void close() override;

        bool put(const string& cf_name, const string& key, const string& value) override;
        optional<string> get(const string& cf_name, const string& key) override;
        bool del(const string& cf_name, const string& key) override;

        static string buildDomainKey(const string& reversed_host, const string& path, uint64_t doc_id);

        uint64_t getNextDocId();
        void setNextDocId(uint64_t id);

        void savePendingURLs(const vector<string>& urls);
        vector<string> loadPendingURLs();
        vector<string> getUrlsBatch(const uint64_t limit = 10000);
        vector<string> getAllDocumentUrls(const uint64_t limit = 10000);
        void recordCrawlResult(
            const string& url_hash,
            const string& normalized_url,
            int http_status,
            const string& content_hash,
            bool content_changed
        );
        DuplicateRemovalStats removeDuplicateURLs();
        unordered_map<uint64_t, uint64_t> loadClickCounts();
        uint64_t incrementClickCount(uint64_t doc_id);
        vector<SearchDocumentRecord> loadSearchDocuments();
        void forEachSearchDocument(const function<void(SearchDocumentRecord&&)>& visitor);

    private:
        rocksdb::DB* db_ = nullptr;
        map<string, rocksdb::ColumnFamilyHandle*> handles_;
        mutex mutex_;
        string db_path_;

        void setupColumnFamilies(rocksdb::Options& options, vector<rocksdb::ColumnFamilyDescriptor>& column_families);
    };

} // namespace storage

#endif // ROCKSDB_STORE_H
