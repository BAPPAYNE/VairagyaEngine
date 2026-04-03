#include "storage/rocksdb_store.h"
#include "storage/db_schema.h"
#include <iostream>
#include <vector>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>

namespace storage {

    RocksDBStore::RocksDBStore() {}

    RocksDBStore::~RocksDBStore() {
        close();
    }

    void RocksDBStore::setupColumnFamilies(rocksdb::Options& options, std::vector<rocksdb::ColumnFamilyDescriptor>& column_families) {
        column_families.push_back({rocksdb::kDefaultColumnFamilyName, rocksdb::ColumnFamilyOptions()});
        column_families.push_back({CF_DOC_CORE, rocksdb::ColumnFamilyOptions()});
        column_families.push_back({CF_DOMAIN_INDEX, rocksdb::ColumnFamilyOptions()});
        column_families.push_back({CF_FETCH_META, rocksdb::ColumnFamilyOptions()});
        column_families.push_back({CF_CONTENT_META, rocksdb::ColumnFamilyOptions()});
        column_families.push_back({CF_PARSED_CONTENT, rocksdb::ColumnFamilyOptions()});
        column_families.push_back({CF_LINK_GRAPH, rocksdb::ColumnFamilyOptions()});
        column_families.push_back({CF_QUALITY, rocksdb::ColumnFamilyOptions()});
        column_families.push_back({CF_PRESENTATION, rocksdb::ColumnFamilyOptions()});
        column_families.push_back({CF_CONTROL, rocksdb::ColumnFamilyOptions()});
    }

    bool RocksDBStore::open(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (db_) return true;

        rocksdb::DBOptions db_options;
        db_options.create_if_missing = true;
        db_options.create_missing_column_families = true;

        rocksdb::Options options;
        std::vector<rocksdb::ColumnFamilyDescriptor> column_families;
        setupColumnFamilies(options, column_families);

        std::vector<rocksdb::ColumnFamilyHandle*> handles;
        rocksdb::Status status = rocksdb::DB::Open(db_options, path, column_families, &handles, &db_);

        if (!status.ok()) {
            std::cerr << "[ERROR] RocksDB open failed: " << status.ToString() << std::endl;
            return false;
        }

        for (size_t i = 0; i < column_families.size(); ++i) {
            handles_[column_families[i].name] = handles[i];
        }

        db_path_ = path;
        return true;
    }

    void RocksDBStore::close() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (db_) {
            for (auto const& [name, handle] : handles_) {
                delete handle;
            }
            handles_.clear();
            delete db_;
            db_ = nullptr;
        }
    }

    bool RocksDBStore::put(const std::string& cf_name, const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return false;

        auto it = handles_.find(cf_name);
        if (it == handles_.end()) {
            std::cerr << "[ERROR] Column Family not found: " << cf_name << std::endl;
            return false;
        }

        rocksdb::Status status = db_->Put(rocksdb::WriteOptions(), it->second, key, value);
        return status.ok();
    }

    std::optional<std::string> RocksDBStore::get(const std::string& cf_name, const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return std::nullopt;

        auto it = handles_.find(cf_name);
        if (it == handles_.end()) {
            std::cerr << "[ERROR] Column Family not found: " << cf_name << std::endl;
            return std::nullopt;
        }

        std::string value;
        rocksdb::Status status = db_->Get(rocksdb::ReadOptions(), it->second, key, &value);

        if (status.IsNotFound()) return std::nullopt;
        if (!status.ok()) {
            std::cerr << "[ERROR] RocksDB get failed: " << status.ToString() << std::endl;
            return std::nullopt;
        }

        return value;
    }

    bool RocksDBStore::del(const std::string& cf_name, const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return false;

        auto it = handles_.find(cf_name);
        if (it == handles_.end()) return false;

        rocksdb::Status status = db_->Delete(rocksdb::WriteOptions(), it->second, key);
        return status.ok();
    }

    std::string RocksDBStore::buildDomainKey(const std::string& reversed_host, const std::string& path, uint64_t doc_id) {
        return "d:" + reversed_host + "|" + path + "|" + std::to_string(doc_id);
    }

    uint64_t RocksDBStore::getNextDocId() {
        auto val = get(CF_DEFAULT, "next_doc_id");
        if (!val) return 1;
        try {
            return std::stoull(*val);
        } catch (...) {
            return 1;
        }
    }

    void RocksDBStore::setNextDocId(uint64_t id) {
        put(CF_DEFAULT, "next_doc_id", std::to_string(id));
    }

} // namespace storage
