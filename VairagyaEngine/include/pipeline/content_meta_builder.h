#ifndef CONTENT_META_BUILDER_H
#define CONTENT_META_BUILDER_H

#include <storage/db_schema.h>
#include <string>

class ContentMetaBuilder {
public:
    static storage::ContentMeta build(const std::string& clean_text, uint64_t canonical_doc_id = 0); // build content meta from clean text
    static uint64_t computeSimhash(const std::string& text); // compute simhash of the text
};

#endif // CONTENT_META_BUILDER_H
