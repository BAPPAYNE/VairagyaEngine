#include "utils/utils.h" 

#include <sys/stat.h>
#include <fstream>
#include <algorithm>

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