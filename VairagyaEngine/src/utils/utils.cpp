#include "utils/utils.h" 

#include <sys/stat.h>
#include <fstream>

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