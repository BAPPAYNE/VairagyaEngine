#include "pipeline/parsed_content_builder.h"

#include <algorithm>
#include <cctype>
#include <sstream>

using namespace std;
using namespace storage;

namespace {
    constexpr size_t MAX_HTML_BYTES = 4u * 1024 * 1024;
    constexpr size_t MAX_HEAD_BYTES = 128u * 1024;

    string toLowerAscii(string value) {
        transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(tolower(c));
        });
        return value;
    }

    void trimInPlace(string& value) {
        const auto begin = find_if_not(value.begin(), value.end(), [](unsigned char c) {
            return isspace(c);
        });
        const auto end = find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
            return isspace(c);
        }).base();

        if (begin >= end) {
            value.clear();
            return;
        }
        value.assign(begin, end);
    }

    bool startsWithIgnoreCase(const string& text, size_t pos, const string& needle) {
        if (pos + needle.size() > text.size()) {
            return false;
        }
        for (size_t i = 0; i < needle.size(); ++i) {
            if (tolower(static_cast<unsigned char>(text[pos + i])) !=
                tolower(static_cast<unsigned char>(needle[i]))) {
                return false;
            }
        }
        return true;
    }

    size_t findIgnoreCase(const string& text, const string& needle, size_t pos = 0) {
        if (needle.empty() || needle.size() > text.size()) {
            return string::npos;
        }
        for (size_t i = pos; i + needle.size() <= text.size(); ++i) {
            if (startsWithIgnoreCase(text, i, needle)) {
                return i;
            }
        }
        return string::npos;
    }

    string getAttributeValue(const string& tag, const string& attr_name) {
        const string lower = toLowerAscii(tag);
        const string attr = toLowerAscii(attr_name);
        size_t pos = 0;

        while ((pos = lower.find(attr, pos)) != string::npos) {
            const bool left_ok = pos == 0 ||
                !(isalnum(static_cast<unsigned char>(lower[pos - 1])) || lower[pos - 1] == '-' || lower[pos - 1] == '_');
            const size_t after = pos + attr.size();
            const bool right_ok = after >= lower.size() ||
                !(isalnum(static_cast<unsigned char>(lower[after])) || lower[after] == '-' || lower[after] == '_');

            if (!left_ok || !right_ok) {
                pos = after;
                continue;
            }

            size_t eq = lower.find('=', after);
            if (eq == string::npos) {
                return "";
            }
            for (size_t i = after; i < eq; ++i) {
                if (!isspace(static_cast<unsigned char>(lower[i]))) {
                    pos = after;
                    eq = string::npos;
                    break;
                }
            }
            if (eq == string::npos) {
                continue;
            }

            size_t value_start = eq + 1;
            while (value_start < tag.size() && isspace(static_cast<unsigned char>(tag[value_start]))) {
                ++value_start;
            }
            if (value_start >= tag.size()) {
                return "";
            }

            char quote = 0;
            if (tag[value_start] == '"' || tag[value_start] == '\'') {
                quote = tag[value_start++];
            }

            size_t value_end = value_start;
            if (quote) {
                value_end = tag.find(quote, value_start);
                if (value_end == string::npos) {
                    value_end = tag.size();
                }
            }
            else {
                while (value_end < tag.size() &&
                    !isspace(static_cast<unsigned char>(tag[value_end])) &&
                    tag[value_end] != '>') {
                    ++value_end;
                }
            }

            string value = tag.substr(value_start, value_end - value_start);
            trimInPlace(value);
            return value;
        }

        return "";
    }

    string collapseWhitespace(const string& input) {
        string output;
        output.reserve(input.size());

        bool previous_space = true;
        for (unsigned char c : input) {
            if (isspace(c)) {
                if (!previous_space) {
                    output.push_back(' ');
                    previous_space = true;
                }
            }
            else {
                output.push_back(static_cast<char>(c));
                previous_space = false;
            }
        }

        if (!output.empty() && output.back() == ' ') {
            output.pop_back();
        }
        return output;
    }
}

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
    const string src = html.size() > MAX_HEAD_BYTES ? html.substr(0, MAX_HEAD_BYTES) : html;
    const size_t title_start = findIgnoreCase(src, "<title");
    if (title_start == string::npos) {
        return "";
    }

    const size_t open_end = src.find('>', title_start);
    if (open_end == string::npos) {
        return "";
    }

    const size_t close_start = findIgnoreCase(src, "</title>", open_end + 1);
    if (close_start == string::npos || close_start <= open_end + 1) {
        return "";
    }

    string title = src.substr(open_end + 1, close_start - open_end - 1);
    trimInPlace(title);
    return title;
}

string ParsedContentBuilder::extractDescription(const string& html) {
    const string src = html.size() > MAX_HEAD_BYTES ? html.substr(0, MAX_HEAD_BYTES) : html;
    size_t pos = 0;

    while ((pos = findIgnoreCase(src, "<meta", pos)) != string::npos) {
        const size_t end = src.find('>', pos);
        if (end == string::npos) {
            break;
        }

        const string tag = src.substr(pos, end - pos + 1);
        if (toLowerAscii(getAttributeValue(tag, "name")) == "description") {
            return getAttributeValue(tag, "content");
        }

        pos = end + 1;
    }

    return "";
}

string ParsedContentBuilder::cleanText(const string& html) {
    const string src = html.size() > MAX_HTML_BYTES ? html.substr(0, MAX_HTML_BYTES) : html;

    string text;
    text.reserve(src.size());

    for (size_t i = 0; i < src.size();) {
        if (startsWithIgnoreCase(src, i, "<!--")) {
            const size_t end = src.find("-->", i + 4);
            i = end == string::npos ? src.size() : end + 3;
            text.push_back(' ');
            continue;
        }

        if (src[i] == '<') {
            size_t name_start = i + 1;
            if (name_start < src.size() && src[name_start] == '/') {
                ++name_start;
            }
            while (name_start < src.size() && isspace(static_cast<unsigned char>(src[name_start]))) {
                ++name_start;
            }

            size_t name_end = name_start;
            while (name_end < src.size() && isalpha(static_cast<unsigned char>(src[name_end]))) {
                ++name_end;
            }

            const string tag = toLowerAscii(src.substr(name_start, name_end - name_start));
            if (tag == "script" || tag == "style" || tag == "noscript" || tag == "svg" || tag == "canvas") {
                const string close_tag = "</" + tag + ">";
                const size_t end = findIgnoreCase(src, close_tag, name_end);
                i = end == string::npos ? src.size() : end + close_tag.size();
                text.push_back(' ');
                continue;
            }

            const size_t end = src.find('>', i + 1);
            i = end == string::npos ? src.size() : end + 1;
            text.push_back(' ');
            continue;
        }

        text.push_back(src[i]);
        ++i;
    }

    return collapseWhitespace(text);
}
