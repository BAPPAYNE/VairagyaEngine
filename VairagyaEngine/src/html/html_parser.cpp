#include "html/html_parser.h"
#include "url/process.h"

#include <iostream>
#include <string>
#include <vector>
#include <regex>

using namespace std;

static regex href_regex(R"(href\s*=\s*["']?([^\s"'<>#]+))", regex::icase); // parses href="<href_value> 

/* other examples : 
    (http|ftp|https):\/\/([\w_-]+(?:(?:\.[\w_-]+)+))([\w.,@?^=% &:\/~#-]*[\w@?^=% &\/~+#-])
    (http|ftp|https)://([\w_-]+(?:(?:\.[\w_-]+)+))([\w.,@?^=%&:/~+#-]*[\w@?^=%&/~+#-])?
    (?:(?:https?|ftp|file):\/\/|www\.|ftp\.)(?:\([-A-Z0-9+&@#\/%=~_|$?!:,.]*\)|[-A-Z0-9+&@#\/%=~_|$?!:,.])*(?:\([-A-Z0-9+&@#\/%=~_|$?!:,.]*\)|[A-Z0-9+&@#\/%=~_|$])
*/

vector<string> extractLinks(const string& content) {
    vector<string> links;

    auto begin = sregex_iterator(content.begin(), content.end(), href_regex);
    auto end = sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        string raw_url = (*it)[1].str();
        links.emplace_back(raw_url);
    }

    return links;
}
