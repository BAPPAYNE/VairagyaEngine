#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <unordered_set>

using namespace std;

// Configuration constants for VairagyaEngine

extern string list_path;          // Path to the list of URLs
extern bool crawl_links;        // Enable link extraction and recursive crawling
extern bool verbose_logging;    // Enable verbose logging
extern string json_output_path;       // JSON output file name
extern string txt_output_path;        // TXT output file name
extern bool same_domain;              // Restrict crawling to same domain only
extern bool ignore_robots;            // Ignore robots.txt rules
extern unordered_set<string> allowed_domains;  // Set of allowed domains for crawling

#endif // CONFIG_H
