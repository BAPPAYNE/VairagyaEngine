#ifndef DB_SCHEMA_H
#define DB_SCHEMA_H

#include <cstdint>
#include <string>
#include <vector>
#include <ctime>
#include <nlohmann/json.hpp>

using namespace std;

namespace storage {

    // Identity Layer (doc_core_cf)
    struct DocCore {
        uint64_t doc_id = 0; // internal primary key
        string normalized_url; // normalized URL
        string url_hash; // hash of the URL, used for quick lookup
        string canonical_url; // normalized URL, used for deduplication
        time_t first_seen_time = 0; // timestamp of when the URL was discovered
        string language_code; // ISO 639-1 language code, e.g., "en" for English
        string charset; // character encoding of the document, e.g., "UTF-8, ASCII, ANSI"
        string content_type; // MIME type of the document, e.g., "text/html"

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(DocCore, doc_id, normalized_url, url_hash, canonical_url, first_seen_time, language_code, charset, content_type)
    };

    // Fetch Metadata (fetch_meta_cf)
    struct FetchMeta {
        time_t last_fetched_time = 0;
        int fetch_status_code = 0;
        int fetch_latency_ms = 0;
        size_t content_length_bytes = 0;
        string etag;
        string last_modified;
        int crawl_depth = 0;
        string referrer_url;
        int crawl_priority = 0;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(FetchMeta, last_fetched_time, fetch_status_code, fetch_latency_ms, content_length_bytes, etag, last_modified, crawl_depth, referrer_url, crawl_priority)
    };

    // Content Fingerprinting (content_meta_cf)
    struct ContentMeta {
        string content_hash;
        uint64_t simhash = 0;
        bool is_duplicate = false;
        uint64_t canonical_doc_id = 0;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(ContentMeta, content_hash, simhash, is_duplicate, canonical_doc_id)
    };

    // Parsed Content (search core) (parsed_content_cf)
    struct ParsedContent {
        string title;
        string meta_description;
        string clean_text;
        uint32_t token_count = 0;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(ParsedContent, title, meta_description, clean_text, token_count)
    };

    // Link Analysis (link_graph_cf)
    struct LinkData {
        uint32_t outbound_links_count = 0;
        uint32_t inbound_links_count = 0;
        float pagerank_score = 0.0f;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(LinkData, outbound_links_count, inbound_links_count, pagerank_score)
    };

    // Quality Signals (quality_cf)
    struct QualitySignals {
        time_t content_last_changed_time = 0;
        float update_frequency = 0.0f;
        float spam_score = 0.0f;
        float quality_score = 0.0f;
        float readability_score = 0.0f;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(QualitySignals, content_last_changed_time, update_frequency, spam_score, quality_score, readability_score)
    };

    // Presentation Layer (presentation_cf)
    struct Presentation {
        string snippet;
        string favicon_url;
        string site_name;
        string breadcrumb;
        string display_url;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Presentation, snippet, favicon_url, site_name, breadcrumb, display_url)
    };

    // Control & Debug (control_cf)
    struct ControlFlags {
        bool robots_allowed = true;
        bool noindex = false;
        bool nofollow = false;
        string index_status;
        string error_reason;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(ControlFlags, robots_allowed, noindex, nofollow, index_status, error_reason)
    };

    // Names for Column Families
    const string CF_DOC_CORE = "doc_core_cf";
    const string CF_DOMAIN_INDEX = "domain_index_cf";
    const string CF_FETCH_META = "fetch_meta_cf";
    const string CF_CONTENT_META = "content_meta_cf";
    const string CF_PARSED_CONTENT = "parsed_content_cf";
    const string CF_LINK_GRAPH = "link_graph_cf";
    const string CF_QUALITY = "quality_cf";
    const string CF_PRESENTATION = "presentation_cf";
    const string CF_CONTROL = "control_cf";
    const string CF_DEFAULT = "default";

} // namespace storage

#endif // DB_SCHEMA_H
