#ifndef FETCH_META_BUILDER_H
#define FETCH_META_BUILDER_H

#include <storage/db_schema.h>
#include <string>

using namespace std;

class FetchMetaBuilder {
public:
    static storage::FetchMeta build(
        int status_code, // HTTP status code, e.g., 200, 404, 500
        int latency_ms, // time in milliseconds to fetch the document
        size_t content_length, // size of the document in bytes
        const string& etag, // ETag header value
        const string& last_modified, // Last-Modified header value
        int crawl_depth, // depth of the crawl, e.g., 0 for seed URLs, 1 for first-level links, etc.
        const string& referrer, // URL of the page that linked to this document
        int priority = 0 // priority of the crawl, e.g., 0 for seed URLs, 1 for first-level links, etc.
    );
};

#endif // FETCH_META_BUILDER_H
