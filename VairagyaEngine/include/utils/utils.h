#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <vector>

using namespace std;

bool isValidPath(const string& path); // validate path
    
vector<string> fetchLinesFromFile(const string& path); // fetch lines from file

#endif // UTILS_H
