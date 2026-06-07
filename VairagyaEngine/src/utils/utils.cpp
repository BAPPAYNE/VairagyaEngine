#include "utils/utils.h"

#include <sys/stat.h>
#include <fstream>
#include <algorithm>
#include <cstdint>   // uint32_t etc. (MSVC pulls this in transitively; GCC/Clang do not)
#include <cctype>
#include <string>

using namespace std;

static struct stat sb;

bool isValidPath(const string& path) {
    if (stat(path.c_str(), &sb) == 0) {
        return true;
    }
    return false;
}

vector<string> fetchLinesFromFile(const string& path) {
    ifstream file(path);

    vector<string> res;
    string line;
    if (file.is_open()) {
        while (getline(file, line)) {
            if (!line.empty() && static_cast<unsigned char>(line.front()) == 0xEF) {
                if (line.size() >= 3 &&
                    static_cast<unsigned char>(line[1]) == 0xBB &&
                    static_cast<unsigned char>(line[2]) == 0xBF) {
                    line.erase(0, 3);
                }
            }

            line.erase(line.begin(), find_if(line.begin(), line.end(), [](unsigned char c) {
                return !isspace(c);
            }));
            line.erase(find_if(line.rbegin(), line.rend(), [](unsigned char c) {
                return !isspace(c);
            }).base(), line.end());

            if (!line.empty()) {
                res.push_back(line);
            }
        }
        file.close();
    }
    return res;
}

bool isHtmlPageUrl(const string& url) {
    static const vector<string> non_html_ext = {
        ".js", ".css", ".png", ".jpg", ".jpeg", ".gif", ".svg", ".ico", ".woff", ".woff2", ".ttf"
    };
    string lower_url = url;
    transform(lower_url.begin(), lower_url.end(), lower_url.begin(), ::tolower);
    for (const auto& ext : non_html_ext) {
        if (lower_url.size() >= ext.size() &&
            lower_url.compare(lower_url.size() - ext.size(), ext.size(), ext) == 0) {
            return false;
        }
    }
    return true;
}

string sanitizeUtf8Lossy(const string& input) {
    static const string replacement = "\xEF\xBF\xBD";

    string output;
    output.reserve(input.size());

    auto appendReplacement = [&output]() {
        output += replacement;
        };

    for (size_t i = 0; i < input.size();) {
        const unsigned char byte = static_cast<unsigned char>(input[i]);

        if (byte <= 0x7F) {
            output.push_back(static_cast<char>(byte));
            ++i;
            continue;
        }

        size_t expected = 0;
        uint32_t codepoint = 0;

        if (byte >= 0xC2 && byte <= 0xDF) {
            expected = 2;
            codepoint = byte & 0x1F;
        }
        else if (byte >= 0xE0 && byte <= 0xEF) {
            expected = 3;
            codepoint = byte & 0x0F;
        }
        else if (byte >= 0xF0 && byte <= 0xF4) {
            expected = 4;
            codepoint = byte & 0x07;
        }
        else {
            appendReplacement();
            ++i;
            continue;
        }

        if (i + expected > input.size()) {
            appendReplacement();
            break;
        }

        bool valid = true;
        for (size_t offset = 1; offset < expected; ++offset) {
            const unsigned char continuation = static_cast<unsigned char>(input[i + offset]);
            if ((continuation & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            codepoint = (codepoint << 6) | (continuation & 0x3F);
        }

        if (valid) {
            if ((expected == 2 && codepoint < 0x80) ||
                (expected == 3 && codepoint < 0x800) ||
                (expected == 4 && codepoint < 0x10000) ||
                (codepoint >= 0xD800 && codepoint <= 0xDFFF) ||
                codepoint > 0x10FFFF) {
                valid = false;
            }
        }

        if (!valid) {
            appendReplacement();
            ++i;
            continue;
        }

        output.append(input, i, expected);
        i += expected;
    }

    return output;
}
