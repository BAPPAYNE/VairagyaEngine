#ifndef CONFIG_H
#define CONFIG_H

#include <string>

using namespace std;

// Configuration constants for VairagyaEngine

extern string list_path;          // Path to the list of URLs
extern bool crawl_links;        // Enable link extraction and recursive crawling
extern bool verbose_logging;    // Enable verbose logging
extern string json_output_path;       // JSON output file name
extern string txt_output_path;        // TXT output file name

#endif // CONFIG_H
