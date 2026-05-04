#include "query/snippet_generator.h"
#include "query/query_processor.h"

#include <algorithm>
#include <cctype>
#include <regex>

using namespace std;

namespace search {

    namespace {
        string collapseWhitespace(const string& text) {
            string output;
            output.reserve(text.size());

            bool previous_space = true;
            for (unsigned char ch : text) {
                if (isspace(ch)) {
                    if (!previous_space) {
                        output.push_back(' ');
                        previous_space = true;
                    }
                } else {
                    output.push_back(static_cast<char>(ch));
                    previous_space = false;
                }
            }

            if (!output.empty() && output.back() == ' ') {
                output.pop_back();
            }
            return output;
        }

        string stripObviousCodeNoise(const string& text) {
            string cleaned = regex_replace(text, regex(R"(\b(function|var|let|const)\s+[A-Za-z0-9_$]+\s*[=\(][^.;{}]*(;|\{[^{}]*\}))"), " ");
            cleaned = regex_replace(cleaned, regex(R"(\b(window|document)\.[A-Za-z0-9_.$]+\s*[=\(][^.;{}]*(;|\{[^{}]*\}))"), " ");
            cleaned = regex_replace(cleaned, regex(R"(@media\s*\([^{}]*\)\s*\{[^{}]*\})"), " ");
            cleaned = regex_replace(cleaned, regex(R"([A-Za-z0-9_$]+\s*:\s*\{[^{}]*\})"), " ");
            return collapseWhitespace(cleaned);
        }
    }

    string SnippetGenerator::generate(
        const string& text,
        const vector<string>& tokens,
        size_t radius
    ) const {
        string clean_text = stripObviousCodeNoise(text);
        if (clean_text.empty()) {
            return "";
        }

        QueryProcessor processor;
        const string normalized_text = processor.normalize(clean_text);
        size_t match = string::npos;

        for (const auto& token : tokens) {
            match = normalized_text.find(token);
            if (match != string::npos) {
                break;
            }
        }

        if (match == string::npos) {
            return clean_text.substr(0, min(radius * 2, clean_text.size()));
        }

        const size_t start = match > radius ? match - radius : 0;
        const size_t end = min(clean_text.size(), match + radius);
        string snippet = clean_text.substr(start, end - start);

        if (start > 0) {
            snippet = "..." + snippet;
        }
        if (end < text.size()) {
            snippet += "...";
        }

        return snippet;
    }

}
