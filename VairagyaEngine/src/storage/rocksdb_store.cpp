#include "storage/rocksdb_store.h"
#include "storage/db_schema.h"

#include <iostream>
#include <vector>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <nlohmann/json.hpp>

namespace storage {

    using json = nlohmann::json;

    RocksDBStore::RocksDBStore() {}

    RocksDBStore::~RocksDBStore() {
        close();
    }

    void RocksDBStore::setupColumnFamilies(rocksdb::Options& options, vector<rocksdb::ColumnFamilyDescriptor>& column_families) {
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

    bool RocksDBStore::open(const string& path) {
        lock_guard<mutex> lock(mutex_);
        if (db_) return true;

        rocksdb::DBOptions db_options;
        db_options.create_if_missing = true;
        db_options.create_missing_column_families = true;

        rocksdb::Options options;
        vector<rocksdb::ColumnFamilyDescriptor> column_families;
        setupColumnFamilies(options, column_families);

        vector<rocksdb::ColumnFamilyHandle*> handles;
        rocksdb::Status status = rocksdb::DB::Open(db_options, path, column_families, &handles, &db_);

        if (!status.ok()) {
            cerr << "[ERROR] RocksDB open failed: " << status.ToString() << endl;
            return false;
        }

        for (size_t i = 0; i < column_families.size(); ++i) {
            handles_[column_families[i].name] = handles[i];
        }

        db_path_ = path;
        return true;
    }

    void RocksDBStore::close() {
        lock_guard<mutex> lock(mutex_);
        if (db_) {
            for (auto const& [name, handle] : handles_) {
                delete handle;
            }
            handles_.clear();
            delete db_;
            db_ = nullptr;
        }
    }

    bool RocksDBStore::put(const string& cf_name, const string& key, const string& value) {
        lock_guard<mutex> lock(mutex_);
        if (!db_) return false;

        auto it = handles_.find(cf_name);
        if (it == handles_.end()) {
            cerr << "[ERROR] Column Family not found: " << cf_name << endl;
            return false;
        }

        rocksdb::Status status = db_->Put(rocksdb::WriteOptions(), it->second, key, value);
        return status.ok();
    }

    optional<string> RocksDBStore::get(const string& cf_name, const string& key) {
        lock_guard<mutex> lock(mutex_);
        if (!db_) return nullopt;

        auto it = handles_.find(cf_name);
        if (it == handles_.end()) {
            cerr << "[ERROR] Column Family not found: " << cf_name << endl;
            return nullopt;
        }

        string value;
        rocksdb::Status status = db_->Get(rocksdb::ReadOptions(), it->second, key, &value);

        if (status.IsNotFound()) return nullopt;
        if (!status.ok()) {
            cerr << "[ERROR] RocksDB get failed: " << status.ToString() << endl;
            return nullopt;
        }

        return value;
    }

    bool RocksDBStore::del(const string& cf_name, const string& key) {
        lock_guard<mutex> lock(mutex_);
        if (!db_) return false;

        auto it = handles_.find(cf_name);
        if (it == handles_.end()) return false;

        rocksdb::Status status = db_->Delete(rocksdb::WriteOptions(), it->second, key);
        return status.ok();
    }

    string RocksDBStore::buildDomainKey(const string& reversed_host, const string& path, uint64_t doc_id) {
        return "d:" + reversed_host + "|" + path + "|" + to_string(doc_id);
    }

    uint64_t RocksDBStore::getNextDocId() {
        auto val = get(CF_DEFAULT, "next_doc_id");
        if (!val) return 1;
        try {
            return stoull(*val);
        } catch (...) {
            return 1;
        }
    }

    void RocksDBStore::setNextDocId(uint64_t id) {
        put(CF_DEFAULT, "next_doc_id", to_string(id));
    }

    // Use a dedicated key for pending URLs
    const string PENDING_URLS_KEY = "__pending_urls__";

    void RocksDBStore::savePendingURLs(const vector<string>& urls) {
        lock_guard<mutex> lock(mutex_);
        if (!db_) return;
        string data = json(urls).dump();
        rocksdb::WriteOptions write_options;
        write_options.sync = true;
        db_->Put(write_options, handles_[rocksdb::kDefaultColumnFamilyName], PENDING_URLS_KEY, data);
    }

    vector<string> RocksDBStore::loadPendingURLs() {
        lock_guard<mutex> lock(mutex_);
        vector<string> urls;
        if (!db_) return urls;
        string data;
        auto status = db_->Get(rocksdb::ReadOptions(), handles_[rocksdb::kDefaultColumnFamilyName], PENDING_URLS_KEY, &data);
        if (status.ok() && !data.empty()) {
            try {
                urls = json::parse(data).get<vector<string>>();
            } catch (...) {}
        }
        return urls;
    }

    vector<string> RocksDBStore::getUrlsBatch(uint64_t limit) {
        lock_guard<mutex> lock(mutex_);

        vector<string> urls;
        urls.reserve(static_cast<size_t>(limit));

        if (!db_ || limit == 0)
            return urls;

        auto domain_it = handles_.find(CF_DOMAIN_INDEX);
        auto doc_core_it = handles_.find(CF_DOC_CORE);
        if (domain_it == handles_.end() || doc_core_it == handles_.end())
            return urls;

        const string cursor_key = "__scan_cursor__";
        string last_key;

        // load previous cursor
        auto status = db_->Get(
            rocksdb::ReadOptions(),
            handles_[rocksdb::kDefaultColumnFamilyName],
            cursor_key,
            &last_key
        );

        rocksdb::ReadOptions ro;
        ro.fill_cache = false;          // don't pollute block cache
        ro.total_order_seek = false;
        ro.prefix_same_as_start = true;

        unique_ptr<rocksdb::Iterator> it(
            db_->NewIterator(ro, domain_it->second)
        );

        if (status.ok() && !last_key.empty()) {
            it->Seek(last_key);
            if (it->Valid() && it->key().ToString() == last_key)
                it->Next(); // continue after cursor
        }
        else {
            it->Seek("d:");
        }

        string last_processed;

        for (; it->Valid() && urls.size() < limit; it->Next()) {
            auto key = it->key().ToString();

            if (key.rfind("d:", 0) != 0)
                break; // prefix ended

            const string url_hash = it->value().ToString();
            string doc_core_json;
            auto doc_status = db_->Get(
                rocksdb::ReadOptions(),
                doc_core_it->second,
                url_hash,
                &doc_core_json
            );

            if (doc_status.ok() && !doc_core_json.empty()) {
                try {
                    auto doc = json::parse(doc_core_json).get<DocCore>();
                    if (!doc.normalized_url.empty()) {
                        urls.emplace_back(doc.normalized_url);
                    }
                } catch (...) {
                    // Skip malformed doc_core records and continue scanning.
                }
            }
            last_processed = key;
        }

        // save next cursor
        if (!last_processed.empty()) {
            db_->Put(
                rocksdb::WriteOptions(),
                handles_[rocksdb::kDefaultColumnFamilyName],
                cursor_key,
                last_processed
            );
        }
        else {
            // reached end, reset cursor
            db_->Delete(
                rocksdb::WriteOptions(),
                handles_[rocksdb::kDefaultColumnFamilyName],
                cursor_key
            );
        }

        return urls;
    }

} // namespace storage
