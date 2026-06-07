#ifndef QUERY_SNIPPET_GENERATOR_H
#define QUERY_SNIPPET_GENERATOR_H

#include <string>
#include <vector>

using namespace std;

namespace search {

    class SnippetGenerator {
    public:
        string generate(
            const string& text,
            const vector<string>& tokens,
            size_t radius = 90
        ) const;
    };

}

#endif
