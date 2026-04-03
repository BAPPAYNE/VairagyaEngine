#ifndef ROCKSDB_STORE_H
#define ROCKSDB_STORE_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include "storage/kv_store.h"
#include <rocksdb/db.h>

namespace storage {

    class RocksDBStore : public KVStore {
    public:
        RocksDBStore();
        ~RocksDBStore() override;

        bool open(const std::string& path) override;
        void close() override;

        bool put(const std::string& cf_name, const std::string& key, const std::string& value) override;
        std::optional<std::string> get(const std::string& cf_name, const std::string& key) override;
        bool del(const std::string& cf_name, const std::string& key) override;

        static std::string buildDomainKey(const std::string& reversed_host, const std::string& path, uint64_t doc_id);

        uint64_t getNextDocId();
        void setNextDocId(uint64_t id);

    private:
        rocksdb::DB* db_ = nullptr;
        std::map<std::string, rocksdb::ColumnFamilyHandle*> handles_;
        std::mutex mutex_;
        std::string db_path_;

        void setupColumnFamilies(rocksdb::Options& options, std::vector<rocksdb::ColumnFamilyDescriptor>& column_families);
    };

} // namespace storage

#endif // ROCKSDB_STORE_H
