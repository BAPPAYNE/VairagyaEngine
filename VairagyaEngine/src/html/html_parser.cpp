#include "html/html_parser.hpp"
#include "url/process.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

namespace {
    constexpr size_t MAX_LINK_SCAN_BYTES = 2u * 1024 * 1024;

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

    string tagName(const string& tag) {
        size_t pos = tag.find('<');
        if (pos == string::npos) {
            return "";
        }
        ++pos;
        if (pos < tag.size() && tag[pos] == '/') {
            ++pos;
        }
        while (pos < tag.size() && isspace(static_cast<unsigned char>(tag[pos]))) {
            ++pos;
        }

        size_t end = pos;
        while (end < tag.size() &&
            (isalnum(static_cast<unsigned char>(tag[end])) || tag[end] == '-' || tag[end] == ':')) {
            ++end;
        }

        return toLowerAscii(tag.substr(pos, end - pos));
    }
}

vector<string> extractLinks(const string& content) {
    vector<string> links;
    const string src = content.size() > MAX_LINK_SCAN_BYTES ? content.substr(0, MAX_LINK_SCAN_BYTES) : content;

    for (size_t pos = 0; (pos = src.find('<', pos)) != string::npos;) {
        if (startsWithIgnoreCase(src, pos, "<!--")) {
            const size_t comment_end = src.find("-->", pos + 4);
            pos = comment_end == string::npos ? src.size() : comment_end + 3;
            continue;
        }

        const size_t tag_end = src.find('>', pos + 1);
        if (tag_end == string::npos) {
            break;
        }

        const string tag = src.substr(pos, tag_end - pos + 1);
        for (const string& attr : {"href", "src", "loc"}) {
            string value = getAttributeValue(tag, attr);
            if (!value.empty()) {
                links.emplace_back(move(value));
            }
        }

        const string name = tagName(tag);
        if (name == "loc" || name == "link") {
            const string close_tag = "</" + name + ">";
            const size_t body_start = tag_end + 1;
            const size_t body_end = findIgnoreCase(src, close_tag, body_start);
            if (body_end != string::npos) {
                string value = src.substr(body_start, body_end - body_start);
                trimInPlace(value);
                if (!value.empty()) {
                    links.emplace_back(move(value));
                }
            }
        }

        pos = tag_end + 1;
    }

    return links;
}
