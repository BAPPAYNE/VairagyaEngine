#include "utils/config.h"

using namespace std;

string list_path;          // Path to the list of URLs
bool crawl_links = false;        // Enable link extraction and recursive crawling
bool verbose_logging = false;    // Enable verbose logging
string json_output_path;       // JSON output file name
string txt_output_path;        // TXT output file name
bool same_domain = false;        // Restrict crawling to same domain only
bool ignore_robots = false;       // Ignore robots.txt rules
unordered_set<string> allowed_domains;  // Set of allowed domains for crawling
