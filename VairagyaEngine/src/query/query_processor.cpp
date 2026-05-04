#include "query/query_processor.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <unordered_set>
#include <unordered_map>
#include <nlohmann/json.hpp>

using namespace std;

namespace search {

    using json = nlohmann::json;

    namespace {
        const filesystem::path SYNONYMS_JSON_PATH = "VairagyaEngine/config/synonyms.json";

        bool isMeaningfulSymbol(unsigned char ch) {
            return ch == '+' || ch == '#';
        }

        unordered_map<string, vector<string>> loadSynonyms() {
            unordered_map<string, vector<string>> synonym_map;
            ifstream file(SYNONYMS_JSON_PATH);
            if (!file.is_open()) {
                return synonym_map;
            }

            json data = json::parse(file, nullptr, false);
            if (!data.is_object()) {
                return synonym_map;
            }

            for (auto& [key, value] : data.items()) {
                if (value.is_array()) {
                    synonym_map.emplace(key, value.get<vector<string>>());
                }
            }

            return synonym_map;
        }

        const unordered_map<string, vector<string>>& synonyms() {
            static const unordered_map<string, vector<string>> synonym_map = loadSynonyms();
            return synonym_map;
        }

        void addExpandedToken(const string& token, vector<string>& tokens, unordered_set<string>& seen) {
            if (!token.empty() && seen.insert(token).second) {
                tokens.push_back(token);
            }

            auto it = synonyms().find(token);
            if (it == synonyms().end()) {
                return;
            }

            for (const auto& synonym : it->second) {
                if (seen.insert(synonym).second) {
                    tokens.push_back(synonym);
                }
            }
        }
    }

    ProcessedQuery QueryProcessor::process(const string& input) const {
        ProcessedQuery query;
        query.original = input;
        query.normalized = normalize(input);

        unordered_set<string> seen;
        for (const auto& token : tokenize(query.normalized)) {
            if (stopwords().find(token) != stopwords().end()) {
                continue;
            }

            string stemmed = stem(token);
            addExpandedToken(stemmed, query.tokens, seen);
        }

        return query;
    }

    string QueryProcessor::normalize(const string& input) const {
        string output;
        output.reserve(input.size());

        bool previous_space = true;
        for (unsigned char ch : input) {
            if (ch < 128) {
                if (isalnum(ch) || isMeaningfulSymbol(ch)) {
                    output.push_back(static_cast<char>(tolower(ch)));
                    previous_space = false;
                } else if (isspace(ch) || ispunct(ch)) {
                    if (!previous_space) {
                        output.push_back(' ');
                        previous_space = true;
                    }
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

    vector<string> QueryProcessor::tokenize(const string& normalized) const {
        vector<string> tokens;
        string current;

        for (unsigned char ch : normalized) {
            if (ch < 128 && isspace(ch)) {
                if (!current.empty()) {
                    tokens.push_back(current);
                    current.clear();
                }
            } else {
                current.push_back(static_cast<char>(ch));
            }
        }

        if (!current.empty()) {
            tokens.push_back(current);
        }

        return tokens;
    }

    const unordered_set<string>& QueryProcessor::stopwords() {
        static const unordered_set<string> words = {
            "a", "an", "and", "are", "as", "at", "be", "best", "by", "for",
            "from", "how", "in", "is", "it", "of", "on", "or", "that", "the",
            "this", "to", "was", "what", "when", "where", "which", "with"
        };
        return words;
    }

    string QueryProcessor::stem(const string& token) {
        string out = token;
        if (out.size() > 5 && out.ends_with("ing")) {
            out.resize(out.size() - 3);
        } else if (out.size() > 4 && out.ends_with("ies")) {
            out.resize(out.size() - 3);
            out.push_back('y');
        } else if (out.size() > 3 && out.ends_with("es")) {
            out.resize(out.size() - 2);
        } else if (out.size() > 3 && out.ends_with("s")) {
            out.resize(out.size() - 1);
        }
        return out;
    }

}
