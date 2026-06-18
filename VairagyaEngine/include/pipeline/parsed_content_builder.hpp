#ifndef PARSED_CONTENT_BUILDER_H
#define PARSED_CONTENT_BUILDER_H

#include <storage/db_schema.hpp>
#include <string>

using namespace std;

class ParsedContentBuilder {
public:
    static storage::ParsedContent build(const string& html_content); // build parsed content from html content
    static string extractTitle(const string& html); // extract title from html content
    static string extractDescription(const string& html); // extract description from html content
    static string cleanText(const string& html); // clean html content
};

#endif // PARSED_CONTENT_BUILDER_H
