#ifndef QUERY_PROCESSOR_H
#define QUERY_PROCESSOR_H

#include "query/query_types.hpp"

#include <string>
#include <unordered_set>
#include <vector>

using namespace std;

namespace search {

    class QueryProcessor {
    public:
        ProcessedQuery process(const string& input) const;
        string normalize(const string& input) const;
        vector<string> tokenize(const string& normalized) const;

    private:
        static const unordered_set<string>& stopwords();
        static string stem(const string& token);
    };

}

#endif
