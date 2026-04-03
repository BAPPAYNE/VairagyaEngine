#include "pipeline/fetch_meta_builder.h"
#include <ctime>

using namespace storage;

FetchMeta FetchMetaBuilder::build(
    int status_code, 
    int latency_ms, 
    size_t content_length, 
    const std::string& etag, 
    const std::string& last_modified,
    int crawl_depth,
    const std::string& referrer,
    int priority
) {
    FetchMeta meta;
    meta.last_fetched_time = time(nullptr);
    meta.fetch_status_code = status_code;
    meta.fetch_latency_ms = latency_ms;
    meta.content_length_bytes = content_length;
    meta.etag = etag;
    meta.last_modified = last_modified;
    meta.crawl_depth = crawl_depth;
    meta.referrer_url = referrer;
    meta.crawl_priority = priority;
    return meta;
}
