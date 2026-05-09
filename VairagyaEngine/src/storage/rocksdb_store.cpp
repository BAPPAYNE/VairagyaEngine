#include "storage/rocksdb_store.h"
#include "storage/db_schema.h"

#include <iostream>
#include <vector>
#include <limits>
#include <algorithm>
#include <cmath>
#include <ctime>
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

        struct RecrawlState {
            string normalized_url;
            uint64_t last_crawl_ts = 0;
            uint64_t next_crawl_ts = 0;
            uint32_t failure_count = 0;
            double change_rate = 0.0;
            string last_content_hash;
            int last_http_status = 0;
            int priority_score = 0;
        };

        struct RecrawlCandidate {
            string normalized_url;
            string url_hash;
            uint64_t last_crawl_ts = 0;
            uint64_t next_crawl_ts = 0;
            double score = 0.0;
        };

        string recrawlKey(const string& url_hash) {
            return "recrawl:" + url_hash;
        }

        uint64_t clampInterval(uint64_t value, uint64_t min_value, uint64_t max_value) {
            return max(min_value, min(value, max_value));
        }

        bool isRetryableStatus(int status) {
            return status == 0 || status == 408 || status == 429 || status >= 500;
        }

        uint64_t computeBaseRecrawlInterval(double change_rate, int priority_score) {
            constexpr uint64_t one_hour = 60 * 60;
            constexpr uint64_t one_week = 7 * 24 * one_hour;

            double interval = static_cast<double>(one_week);
            interval -= static_cast<double>(one_week - one_hour) * clamp(change_rate, 0.0, 1.0);
            interval *= 1.0 - (clamp(priority_score, 0, 100) / 100.0 * 0.60);
            return clampInterval(static_cast<uint64_t>(interval), one_hour, one_week);
        }

        uint64_t computeFailureBackoff(uint32_t failure_count) {
            constexpr uint64_t one_hour = 60 * 60;
            constexpr uint64_t one_day = 24 * one_hour;
            const uint32_t capped = min<uint32_t>(failure_count, 8);
            return clampInterval(one_hour << capped, one_hour, 14 * one_day);
        }

        RecrawlState parseRecrawlState(const string& data) {
            RecrawlState state;
            auto parsed = json::parse(data, nullptr, false);
            if (parsed.is_discarded() || !parsed.is_object()) {
                return state;
            }

            state.normalized_url = parsed.value("normalized_url", "");
            state.last_crawl_ts = parsed.value("last_crawl_ts", uint64_t{0});
            state.next_crawl_ts = parsed.value("next_crawl_ts", uint64_t{0});
            state.failure_count = parsed.value("failure_count", uint32_t{0});
            state.change_rate = parsed.value("change_rate", 0.0);
            state.last_content_hash = parsed.value("last_content_hash", "");
            state.last_http_status = parsed.value("last_http_status", 0);
            state.priority_score = parsed.value("priority_score", 0);
            return state;
        }

        string serializeRecrawlState(const RecrawlState& state) {
            json data = {
                {"normalized_url", state.normalized_url},
                {"last_crawl_ts", state.last_crawl_ts},
                {"next_crawl_ts", state.next_crawl_ts},
                {"failure_count", state.failure_count},
                {"change_rate", state.change_rate},
                {"last_content_hash", state.last_content_hash},
                {"last_http_status", state.last_http_status},
                {"priority_score", state.priority_score}
            };
            return data.dump();
        }

        int urlPriorityScore(const string& url) {
            int score = 50;
            const auto slash_count = static_cast<int>(count(url.begin(), url.end(), '/'));
            const int depth = max(0, slash_count - 2);
            score -= depth * 5;

            const string lower = [&]() {
                string copy = url;
                transform(copy.begin(), copy.end(), copy.begin(), ::tolower);
                return copy;
            }();

            if (lower.ends_with("/") || lower.ends_with(".html") || lower.ends_with(".htm")) {
                score += 10;
            }
            if (lower.ends_with(".css") || lower.ends_with(".js") || lower.ends_with(".png") ||
                lower.ends_with(".jpg") || lower.ends_with(".jpeg") || lower.ends_with(".svg") ||
                lower.ends_with(".gif") || lower.ends_with(".ico") || lower.ends_with(".woff") ||
                lower.ends_with(".woff2")) {
                score -= 25;
            }

            return clamp(score, 0, 100);
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
        vector<RecrawlCandidate> candidates;
        candidates.reserve(static_cast<size_t>(limit));

        if (!db_ || limit == 0)
            return urls;

        auto doc_core_it = handles_.find(CF_DOC_CORE);
        auto fetch_meta_it = handles_.find(CF_FETCH_META);
        auto quality_it = handles_.find(CF_QUALITY);
        auto content_meta_it = handles_.find(CF_CONTENT_META);
        auto default_it = handles_.find(rocksdb::kDefaultColumnFamilyName);
        if (doc_core_it == handles_.end() || fetch_meta_it == handles_.end() ||
            quality_it == handles_.end() || content_meta_it == handles_.end() ||
            default_it == handles_.end()) {
            return urls;
        }

        rocksdb::ReadOptions ro;
        ro.fill_cache = false;
        ro.verify_checksums = false;
        ro.readahead_size = 4 << 20;

        const uint64_t now = static_cast<uint64_t>(time(nullptr));
        unique_ptr<rocksdb::Iterator> it(db_->NewIterator(ro, doc_core_it->second));

        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            const string url_hash = it->key().ToString();

            auto doc_json = json::parse(
                it->value().data(),
                it->value().data() + it->value().size(),
                nullptr,
                false
            );
            if (doc_json.is_discarded()) {
                continue;
            }

            DocCore doc;
            try {
                doc = doc_json.get<DocCore>();
            } catch (...) {
                continue;
            }

            if (doc.normalized_url.empty()) {
                continue;
            }

            RecrawlState state;
            string recrawl_json;
            const auto recrawl_status = db_->Get(ro, default_it->second, recrawlKey(url_hash), &recrawl_json);
            if (recrawl_status.ok() && !recrawl_json.empty()) {
                state = parseRecrawlState(recrawl_json);
            }

            FetchMeta fetch;
            string fetch_json;
            const auto fetch_status = db_->Get(ro, fetch_meta_it->second, url_hash, &fetch_json);
            if (fetch_status.ok() && !fetch_json.empty()) {
                try {
                    fetch = json::parse(fetch_json).get<FetchMeta>();
                } catch (...) {
                }
            }

            QualitySignals quality;
            string quality_json;
            const auto quality_status = db_->Get(ro, quality_it->second, url_hash, &quality_json);
            if (quality_status.ok() && !quality_json.empty()) {
                try {
                    quality = json::parse(quality_json).get<QualitySignals>();
                } catch (...) {
                }
            }

            if (state.normalized_url.empty()) {
                state.normalized_url = doc.normalized_url;
            }
            if (state.last_crawl_ts == 0 && fetch.last_fetched_time > 0) {
                state.last_crawl_ts = static_cast<uint64_t>(fetch.last_fetched_time);
            }
            if (state.change_rate <= 0.0 && quality.update_frequency > 0.0f) {
                state.change_rate = clamp(static_cast<double>(quality.update_frequency) / 10.0, 0.0, 1.0);
            }
            if (state.priority_score == 0) {
                state.priority_score = fetch.crawl_priority > 0 ? fetch.crawl_priority : urlPriorityScore(doc.normalized_url);
            }
            if (state.last_http_status == 0) {
                state.last_http_status = fetch.fetch_status_code;
            }
            if (state.next_crawl_ts == 0) {
                const uint64_t interval = isRetryableStatus(state.last_http_status)
                    ? computeFailureBackoff(max<uint32_t>(state.failure_count, 1))
                    : computeBaseRecrawlInterval(state.change_rate, state.priority_score);
                state.next_crawl_ts = state.last_crawl_ts == 0 ? 0 : state.last_crawl_ts + interval;
            }

            if (state.next_crawl_ts > now) {
                continue;
            }

            const uint64_t age = state.last_crawl_ts == 0 ? now : now - min(state.last_crawl_ts, now);
            const uint64_t overdue = state.next_crawl_ts == 0 ? age : now - min(state.next_crawl_ts, now);
            const double freshness_score = min(40.0, static_cast<double>(overdue) / 3600.0);
            const double lru_score = min(25.0, static_cast<double>(age) / 86400.0);
            const double change_score = clamp(state.change_rate, 0.0, 1.0) * 30.0;
            const double importance_score = clamp(state.priority_score, 0, 100) * 0.45;
            const double failure_penalty = state.failure_count > 0 ? min(25.0, state.failure_count * 5.0) : 0.0;

            candidates.push_back({
                doc.normalized_url,
                url_hash,
                state.last_crawl_ts,
                state.next_crawl_ts,
                importance_score + freshness_score + lru_score + change_score - failure_penalty
            });
        }

        sort(candidates.begin(), candidates.end(), [](const RecrawlCandidate& left, const RecrawlCandidate& right) {
            if (left.score != right.score) {
                return left.score > right.score;
            }
            return left.last_crawl_ts < right.last_crawl_ts;
        });

        urls.reserve(static_cast<size_t>(min<uint64_t>(limit, candidates.size())));
        for (const auto& candidate : candidates) {
            if (urls.size() >= limit) {
                break;
            }
            urls.push_back(candidate.normalized_url);
        }

        cout << "[RECRAWL] Selected " << urls.size() << " due URL(s) from "
             << candidates.size() << " due candidate(s).\n";
        return urls;
    }

    void RocksDBStore::recordCrawlResult(
        const string& url_hash,
        const string& normalized_url,
        int http_status,
        const string& content_hash,
        bool content_changed
    ) {
        lock_guard<mutex> lock(mutex_);
        if (!db_ || url_hash.empty()) {
            return;
        }

        auto default_it = handles_.find(rocksdb::kDefaultColumnFamilyName);
        if (default_it == handles_.end()) {
            return;
        }

        RecrawlState state;
        string data;
        const string key = recrawlKey(url_hash);
        auto status = db_->Get(rocksdb::ReadOptions(), default_it->second, key, &data);
        if (status.ok() && !data.empty()) {
            state = parseRecrawlState(data);
        }

        const uint64_t now = static_cast<uint64_t>(time(nullptr));
        state.normalized_url = normalized_url;
        state.last_crawl_ts = now;
        state.last_http_status = http_status;
        state.priority_score = urlPriorityScore(normalized_url);

        if (isRetryableStatus(http_status)) {
            state.failure_count++;
            state.next_crawl_ts = now + computeFailureBackoff(state.failure_count);
        } else {
            state.failure_count = 0;
            const double sample = content_changed ? 1.0 : 0.0;
            state.change_rate = (state.change_rate * 0.75) + (sample * 0.25);
            state.next_crawl_ts = now + computeBaseRecrawlInterval(state.change_rate, state.priority_score);
        }

        if (!content_hash.empty()) {
            state.last_content_hash = content_hash;
        }

        db_->Put(rocksdb::WriteOptions(), default_it->second, key, serializeRecrawlState(state));
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
        auto link_graph_it = handles_.find(CF_LINK_GRAPH);
        auto quality_it = handles_.find(CF_QUALITY);
        auto presentation_it = handles_.find(CF_PRESENTATION);
        auto control_it = handles_.find(CF_CONTROL);
        if (doc_core_it == handles_.end() ||
            fetch_meta_it == handles_.end() ||
            parsed_content_it == handles_.end() ||
            content_meta_it == handles_.end() ||
            link_graph_it == handles_.end() ||
            quality_it == handles_.end() ||
            presentation_it == handles_.end() ||
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
            link_graph_it->second,
            quality_it->second,
            presentation_it->second,
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
                    auto link_json = json::parse(values[3], nullptr, false);
                    if (!link_json.is_discarded()) {
                        record.link_data = link_json.get<LinkData>();
                    }
                }

                if (statuses[4].ok() && !values[4].empty()) {
                    auto quality_json = json::parse(values[4], nullptr, false);
                    if (!quality_json.is_discarded()) {
                        record.quality_signals = quality_json.get<QualitySignals>();
                    }
                }

                if (statuses[5].ok() && !values[5].empty()) {
                    auto presentation_json = json::parse(values[5], nullptr, false);
                    if (!presentation_json.is_discarded()) {
                        record.presentation = presentation_json.get<Presentation>();
                    }
                }

                if (statuses[6].ok() && !values[6].empty()) {
                    auto control_json = json::parse(values[6], nullptr, false);
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
