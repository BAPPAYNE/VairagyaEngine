#include "pipeline/parsed_content_builder.h"
#include <regex>
#include <sstream>

using namespace std;
using namespace storage;

ParsedContent ParsedContentBuilder::build(const string& html_content) {
    ParsedContent content;
    content.title = extractTitle(html_content);
    content.meta_description = extractDescription(html_content);
    content.clean_text = cleanText(html_content);
    content.token_count = (uint32_t)std::count_if(content.clean_text.begin(), content.clean_text.end(), [](char c) {
        return std::isspace(static_cast<unsigned char>(c));
    }) + 1;
    return content;
}

string ParsedContentBuilder::extractTitle(const string& html) {
    static const regex title_regex(R"(<title\b[^>]*>(.*?)</title>)", regex::icase | regex::optimize);
    smatch m;
    if (regex_search(html, m, title_regex) && m.size() >= 2) {
        return m[1].str();
    }
    return "";
}

string ParsedContentBuilder::extractDescription(const string& html) {
    static const regex desc_regex(R"(<meta\s+name=["']description["']\s+content=["'](.*?)["'])", regex::icase | regex::optimize);
    smatch m;
    if (regex_search(html, m, desc_regex) && m.size() >= 2) {
        return m[1].str();
    }
    return "";
}

string ParsedContentBuilder::cleanText(const string& html) {
    // Basic boilerplate removal: kill scripts, styles, and tags
    string text = regex_replace(html, regex(R"(<(script|style)\b[^>]*>.*?</\1>)", regex::icase | regex::optimize), " ");
    text = regex_replace(text, regex(R"(<[^>]+>)", regex::optimize), " ");
    
    // Normalize whitespace
    text = regex_replace(text, regex(R"(\s+)", regex::optimize), " ");
    return text;
}
