#ifndef ROCKSDB_STORE_H
#define ROCKSDB_STORE_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include "storage/kv_store.h"
#include <rocksdb/db.h>

using namespace std;

namespace storage {

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

    private:
        rocksdb::DB* db_ = nullptr;
        map<string, rocksdb::ColumnFamilyHandle*> handles_;
        mutex mutex_;
        string db_path_;

        void setupColumnFamilies(rocksdb::Options& options, vector<rocksdb::ColumnFamilyDescriptor>& column_families);
    };

} // namespace storage

#endif // ROCKSDB_STORE_H
