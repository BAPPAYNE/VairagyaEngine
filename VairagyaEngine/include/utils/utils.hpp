#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <vector>

using namespace std;

bool isValidPath(const string& path); // validate path
    
vector<string> fetchLinesFromFile(const string& path); // fetch lines from file

bool isHtmlPageUrl(const string& url); // check if URL likely points to an HTML page (basic extension check)

string sanitizeUtf8Lossy(const string& input); // replace malformed UTF-8 with the replacement character

#endif // UTILS_H
