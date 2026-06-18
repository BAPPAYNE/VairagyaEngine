#include "pipeline/control_flags_builder.hpp"

#include <algorithm>
#include <cctype>

using namespace std;
using namespace storage;

namespace {
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

    bool metaRobotsContains(const string& html, const string& token) {
        const string src = html.size() > MAX_HEAD_BYTES ? html.substr(0, MAX_HEAD_BYTES) : html;
        size_t pos = 0;

        while ((pos = findIgnoreCase(src, "<meta", pos)) != string::npos) {
            const size_t end = src.find('>', pos);
            if (end == string::npos) {
                break;
            }

            const string tag = src.substr(pos, end - pos + 1);
            if (toLowerAscii(getAttributeValue(tag, "name")) == "robots") {
                const string content = toLowerAscii(getAttributeValue(tag, "content"));
                if (content.find(token) != string::npos) {
                    return true;
                }
            }

            pos = end + 1;
        }

        return false;
    }
}

ControlFlags ControlFlagsBuilder::build(const string& html, bool robots_allowed) {
    ControlFlags cf;
    cf.robots_allowed = robots_allowed;
    cf.noindex = checkNoIndex(html);
    cf.nofollow = checkNoFollow(html);
    cf.index_status = (cf.noindex ? "SKIPPED_NOINDEX" : "PENDING");
    return cf;
}

bool ControlFlagsBuilder::checkNoIndex(const string& html) {
    return metaRobotsContains(html, "noindex");
}

bool ControlFlagsBuilder::checkNoFollow(const string& html) {
    return metaRobotsContains(html, "nofollow");
}
