#ifndef PARSED_CONTENT_BUILDER_H
#define PARSED_CONTENT_BUILDER_H

#include <storage/db_schema.h>
#include <string>

class ParsedContentBuilder {
public:
    static storage::ParsedContent build(const std::string& html_content); // build parsed content from html content
    static std::string extractTitle(const std::string& html); // extract title from html content
    static std::string extractDescription(const std::string& html); // extract description from html content
    static std::string cleanText(const std::string& html); // clean html content
};

#endif // PARSED_CONTENT_BUILDER_H
