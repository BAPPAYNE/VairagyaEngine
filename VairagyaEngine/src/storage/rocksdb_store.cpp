#include "storage/rocksdb_store.h"
#include "storage/db_schema.h"

#include <iostream>
#include <vector>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <nlohmann/json.hpp>

namespace storage {

    using json = nlohmann::json;

    namespace {
        struct DocRecord {
            string url_hash;
            string normalized_url;
            uint64_t doc_id = 0;
        };

        uint64_t docIdRank(uint64_t doc_id) {
            return doc_id == 0 ? numeric_limits<uint64_t>::max() : doc_id;
        }

        uint64_t extractDocIdFromDomainKey(const string& key) {
            const auto pos = key.find_last_of('|');
            if (pos == string::npos || pos + 1 >= key.size()) {
                return 0;
            }
            try {
                return stoull(key.substr(pos + 1));
            } catch (...) {
                return 0;
            }
        }
    }

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
        db_options.max_open_files = -1;
        db_options.use_adaptive_mutex = true;

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

    DuplicateRemovalStats RocksDBStore::removeDuplicateURLs() {
        lock_guard<mutex> lock(mutex_);

        DuplicateRemovalStats stats;
        if (!db_) return stats;

        auto doc_core_it = handles_.find(CF_DOC_CORE);
        auto domain_it = handles_.find(CF_DOMAIN_INDEX);
        auto default_it = handles_.find(rocksdb::kDefaultColumnFamilyName);
        if (doc_core_it == handles_.end() || domain_it == handles_.end() || default_it == handles_.end()) {
            return stats;
        }

        unordered_map<string, DocRecord> kept_by_url;
        unordered_map<string, uint64_t> kept_doc_id_by_hash;
        unordered_set<string> duplicate_hashes;
        uint64_t max_kept_doc_id = 0;

        {
            unique_ptr<rocksdb::Iterator> it(
                db_->NewIterator(rocksdb::ReadOptions(), doc_core_it->second)
            );

            for (it->SeekToFirst(); it->Valid(); it->Next()) {
                try {
                    auto doc = json::parse(it->value().ToString()).get<DocCore>();
                    if (doc.normalized_url.empty()) {
                        continue;
                    }

                    DocRecord current{it->key().ToString(), doc.normalized_url, doc.doc_id};
                    auto kept = kept_by_url.find(doc.normalized_url);
                    if (kept == kept_by_url.end()) {
                        kept_by_url.emplace(doc.normalized_url, current);
                        continue;
                    }

                    if (docIdRank(current.doc_id) < docIdRank(kept->second.doc_id)) {
                        duplicate_hashes.insert(kept->second.url_hash);
                        kept->second = current;
                    } else if (current.url_hash != kept->second.url_hash) {
                        duplicate_hashes.insert(current.url_hash);
                    }
                } catch (...) {
                    // Leave malformed records untouched; they are not safe to deduplicate.
                }
            }
        }

        for (const auto& [url, record] : kept_by_url) {
            kept_doc_id_by_hash[record.url_hash] = record.doc_id;
            if (record.doc_id > max_kept_doc_id) {
                max_kept_doc_id = record.doc_id;
            }
        }

        const vector<string> url_keyed_families = {
            CF_DOC_CORE,
            CF_FETCH_META,
            CF_CONTENT_META,
            CF_PARSED_CONTENT,
            CF_LINK_GRAPH,
            CF_QUALITY,
            CF_PRESENTATION,
            CF_CONTROL
        };

        for (const auto& duplicate_hash : duplicate_hashes) {
            for (const auto& cf_name : url_keyed_families) {
                auto handle = handles_.find(cf_name);
                if (handle != handles_.end()) {
                    db_->Delete(rocksdb::WriteOptions(), handle->second, duplicate_hash);
                }
            }
            stats.duplicate_doc_records_removed++;
        }

        unordered_map<string, vector<string>> domain_keys_by_hash;
        {
            unique_ptr<rocksdb::Iterator> it(
                db_->NewIterator(rocksdb::ReadOptions(), domain_it->second)
            );

            for (it->SeekToFirst(); it->Valid(); it->Next()) {
                const string key = it->key().ToString();
                const string url_hash = it->value().ToString();
                if (duplicate_hashes.find(url_hash) != duplicate_hashes.end()) {
                    db_->Delete(rocksdb::WriteOptions(), domain_it->second, key);
                    stats.duplicate_domain_index_entries_removed++;
                    continue;
                }
                domain_keys_by_hash[url_hash].push_back(key);
            }
        }

        for (const auto& [url_hash, keys] : domain_keys_by_hash) {
            if (keys.size() <= 1) {
                continue;
            }

            string key_to_keep = keys.front();
            auto kept_doc_id = kept_doc_id_by_hash.find(url_hash);
            if (kept_doc_id != kept_doc_id_by_hash.end()) {
                for (const auto& key : keys) {
                    if (extractDocIdFromDomainKey(key) == kept_doc_id->second) {
                        key_to_keep = key;
                        break;
                    }
                }
            }

            for (const auto& key : keys) {
                if (key != key_to_keep) {
                    db_->Delete(rocksdb::WriteOptions(), domain_it->second, key);
                    stats.duplicate_domain_index_entries_removed++;
                }
            }
        }

        string pending_data;
        auto pending_status = db_->Get(
            rocksdb::ReadOptions(),
            default_it->second,
            PENDING_URLS_KEY,
            &pending_data
        );
        if (pending_status.ok() && !pending_data.empty()) {
            try {
                vector<string> urls = json::parse(pending_data).get<vector<string>>();
                vector<string> unique_urls;
                unordered_set<string> seen_urls;
                unique_urls.reserve(urls.size());

                for (const auto& url : urls) {
                    if (seen_urls.insert(url).second) {
                        unique_urls.push_back(url);
                    }
                }

                if (unique_urls.size() != urls.size()) {
                    stats.duplicate_pending_urls_removed = static_cast<uint64_t>(urls.size() - unique_urls.size());
                    db_->Put(
                        rocksdb::WriteOptions(),
                        default_it->second,
                        PENDING_URLS_KEY,
                        json(unique_urls).dump()
                    );
                }
            } catch (...) {
                // Leave malformed pending state untouched.
            }
        }

        uint64_t current_next_doc_id = 1;
        string next_doc_id_data;
        auto next_doc_status = db_->Get(
            rocksdb::ReadOptions(),
            default_it->second,
            "next_doc_id",
            &next_doc_id_data
        );
        if (next_doc_status.ok() && !next_doc_id_data.empty()) {
            try {
                current_next_doc_id = stoull(next_doc_id_data);
            } catch (...) {
                current_next_doc_id = 1;
            }
        }

        const uint64_t required_next_doc_id = max_kept_doc_id + 1;
        if (required_next_doc_id > current_next_doc_id) {
            db_->Put(
                rocksdb::WriteOptions(),
                default_it->second,
                "next_doc_id",
                to_string(required_next_doc_id)
            );
            stats.next_doc_id_repaired = true;
            stats.next_doc_id = required_next_doc_id;
        } else {
            stats.next_doc_id = current_next_doc_id;
        }

        return stats;
    }

    unordered_map<uint64_t, uint64_t> RocksDBStore::loadClickCounts() {
        lock_guard<mutex> lock(mutex_);

        unordered_map<uint64_t, uint64_t> click_counts;
        if (!db_) return click_counts;

        auto default_it = handles_.find(rocksdb::kDefaultColumnFamilyName);
        if (default_it == handles_.end()) {
            return click_counts;
        }

        rocksdb::ReadOptions read_options;
        read_options.fill_cache = false;
        read_options.verify_checksums = false;
        read_options.readahead_size = 1 << 20;

        unique_ptr<rocksdb::Iterator> it(db_->NewIterator(read_options, default_it->second));
        for (it->Seek("click:"); it->Valid(); it->Next()) {
            const auto key = it->key().ToString();
            if (key.rfind("click:", 0) != 0) {
                break;
            }

            try {
                click_counts.emplace(stoull(key.substr(6)), stoull(it->value().ToString()));
            } catch (...) {
            }
        }

        return click_counts;
    }

    uint64_t RocksDBStore::incrementClickCount(uint64_t doc_id) {
        lock_guard<mutex> lock(mutex_);

        if (!db_) return 0;

        auto default_it = handles_.find(rocksdb::kDefaultColumnFamilyName);
        if (default_it == handles_.end()) {
            return 0;
        }

        const string key = "click:" + to_string(doc_id);
        string value;
        uint64_t count = 0;
        auto status = db_->Get(rocksdb::ReadOptions(), default_it->second, key, &value);
        if (status.ok() && !value.empty()) {
            try {
                count = stoull(value);
            } catch (...) {
                count = 0;
            }
        }

        ++count;
        db_->Put(rocksdb::WriteOptions(), default_it->second, key, to_string(count));
        return count;
    }

    vector<SearchDocumentRecord> RocksDBStore::loadSearchDocuments() {
        vector<SearchDocumentRecord> documents;
        forEachSearchDocument([&](SearchDocumentRecord&& record) {
            documents.push_back(std::move(record));
        });
        return documents;
    }

    void RocksDBStore::forEachSearchDocument(const function<void(SearchDocumentRecord&&)>& visitor) {
        lock_guard<mutex> lock(mutex_);

        if (!db_) return;

        auto doc_core_it = handles_.find(CF_DOC_CORE);
        auto fetch_meta_it = handles_.find(CF_FETCH_META);
        auto parsed_content_it = handles_.find(CF_PARSED_CONTENT);
        auto content_meta_it = handles_.find(CF_CONTENT_META);
        auto quality_it = handles_.find(CF_QUALITY);
        auto control_it = handles_.find(CF_CONTROL);
        if (doc_core_it == handles_.end() ||
            fetch_meta_it == handles_.end() ||
            parsed_content_it == handles_.end() ||
            content_meta_it == handles_.end() ||
            quality_it == handles_.end() ||
            control_it == handles_.end()) {
            return;
        }

        rocksdb::ReadOptions read_options;
        read_options.fill_cache = false;
        read_options.verify_checksums = false;
        read_options.readahead_size = 4 << 20;

        unique_ptr<rocksdb::Iterator> it(db_->NewIterator(read_options, doc_core_it->second));
        vector<rocksdb::ColumnFamilyHandle*> multi_get_handles = {
            fetch_meta_it->second,
            parsed_content_it->second,
            content_meta_it->second,
            quality_it->second,
            control_it->second
        };
        vector<rocksdb::Slice> keys;
        keys.reserve(multi_get_handles.size());
        vector<string> values(multi_get_handles.size());

        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            SearchDocumentRecord record;
            auto doc_core_json = json::parse(
                it->value().data(),
                it->value().data() + it->value().size(),
                nullptr,
                false
            );
            if (doc_core_json.is_discarded()) {
                continue;
            }

            try {
                record.doc_core = doc_core_json.get<DocCore>();
                if (record.doc_core.url_hash.empty() || record.doc_core.normalized_url.empty()) {
                    continue;
                }

                keys.clear();
                for (size_t i = 0; i < multi_get_handles.size(); ++i) {
                    keys.emplace_back(record.doc_core.url_hash);
                    values[i].clear();
                }

                auto statuses = db_->MultiGet(read_options, multi_get_handles, keys, &values);
                if (!statuses[1].ok() || values[1].empty()) {
                    continue;
                }

                if (statuses[0].ok() && !values[0].empty()) {
                    auto fetch_meta_json = json::parse(values[0], nullptr, false);
                    if (!fetch_meta_json.is_discarded()) {
                        record.fetch_meta = fetch_meta_json.get<FetchMeta>();
                    }
                }

                auto parsed_content_json = json::parse(values[1], nullptr, false);
                if (parsed_content_json.is_discarded()) {
                    continue;
                }

                record.parsed_content = parsed_content_json.get<ParsedContent>();

                if (statuses[2].ok() && !values[2].empty()) {
                    auto content_meta_json = json::parse(values[2], nullptr, false);
                    if (!content_meta_json.is_discarded()) {
                        record.content_meta = content_meta_json.get<ContentMeta>();
                    }
                }

                if (statuses[3].ok() && !values[3].empty()) {
                    auto quality_json = json::parse(values[3], nullptr, false);
                    if (!quality_json.is_discarded()) {
                        record.quality_signals = quality_json.get<QualitySignals>();
                    }
                }

                if (statuses[4].ok() && !values[4].empty()) {
                    auto control_json = json::parse(values[4], nullptr, false);
                    if (!control_json.is_discarded()) {
                        record.control_flags = control_json.get<ControlFlags>();
                    }
                }

                visitor(std::move(record));
            } catch (...) {
                // Skip malformed documents so one bad record does not break search startup.
            }
        }
    }

} // namespace storage
