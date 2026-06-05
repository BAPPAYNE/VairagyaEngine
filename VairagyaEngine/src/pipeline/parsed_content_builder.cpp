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
    content.token_count = (uint32_t)count_if(content.clean_text.begin(), content.clean_text.end(), [](char c) {
        return isspace(static_cast<unsigned char>(c));
        }) + 1;
    return content;
}

string ParsedContentBuilder::extractTitle(const string& html) {
    static const regex title_regex(R"(<title\b[^>]*>([\s\S]*?)</title>)", regex::icase | regex::optimize);
    smatch m;
    if (regex_search(html, m, title_regex) && m.size() >= 2) {
        return m[1].str();
    }
    return "";
}

string ParsedContentBuilder::extractDescription(const string& html) {
    static const regex desc_regex(
        R"(<meta\b(?=[^>]*\bname\s*=\s*["']description["'])[^>]*\bcontent\s*=\s*["']([^"']*)["'][^>]*>)",
        regex::icase | regex::optimize
    );
    smatch m;
    if (regex_search(html, m, desc_regex) && m.size() >= 2) {
        return m[1].str();
    }
    return "";
}

string ParsedContentBuilder::cleanText(const string& html) {
    // Compile each pattern once (these were rebuilt on every page) and bound the
    // input: std::regex with a backreference (the </\1> below) backtracks, and on
    // very large pages that can blow the stack. Cap the slice we feed it.
    static constexpr size_t MAX_HTML_BYTES = 4u * 1024 * 1024; // 4 MB
    const string src = html.size() > MAX_HTML_BYTES ? html.substr(0, MAX_HTML_BYTES) : html;

    static const regex script_style(R"(<(script|style|noscript|svg|canvas)\b[^>]*>[\s\S]*?</\1>)", regex::icase | regex::optimize);
    static const regex comments(R"(<!--[\s\S]*?-->)", regex::optimize);
    static const regex tags(R"(<[^>]+>)", regex::optimize);
    static const regex spaces(R"(\s+)", regex::optimize);

    string text = regex_replace(src, script_style, " ");
    text = regex_replace(text, comments, " ");
    text = regex_replace(text, tags, " ");
    text = regex_replace(text, spaces, " ");
    return text;
}