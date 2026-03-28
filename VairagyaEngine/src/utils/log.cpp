#include "utils/log.h"
#include <fstream>
#include <iostream>
#include <map>
#include <unordered_set>
#include <nlohmann/json.hpp>
#include <boost/url.hpp>

void debug(const char* format, ...) {
#ifdef DEBUG
	va_list _ArgList;
	_crt_va_start(_ArgList, _Format);
	vprintf_s(_Format, _ArgList);
	_crt_va_end(_ArgList);
#endif
}

namespace log_utils {

    static std::string g_json_path;
    static std::ofstream txt_file;
    static std::map<std::string, std::unordered_set<std::string>> g_grouped_urls; // domain -> set of paths
    static std::unordered_set<std::string> g_logged_urls; // Track all logged URLs to prevent duplicates

    void init_output_streams(const std::string& json_path, const std::string& txt_path) {
        g_json_path = json_path;
        
        // Load existing JSON data if file exists
        if (!g_json_path.empty()) {
            std::ifstream existing_json(g_json_path);
            if (existing_json.is_open()) {
                try {
                    nlohmann::json j;
                    existing_json >> j;
                    
                    // Load existing data into memory
                    int total_urls_loaded = 0;
                    for (auto& [domain, paths] : j.items()) {
                        if (paths.is_array()) {
                            for (const auto& path : paths) {
                                if (path.is_string()) {
                                    std::string path_str = path.get<std::string>();
                                    g_grouped_urls[domain].insert(path_str);
                                    
                                    // Reconstruct full URL and add to logged URLs set
                                    std::string full_url = "https://" + domain + path_str;
                                    g_logged_urls.insert(full_url);
                                    total_urls_loaded++;
                                }
                            }
                        }
                    }
                    
                    std::cout << "[INFO] Loaded existing JSON file: " << g_json_path 
                              << " (" << total_urls_loaded << " URLs from " 
                              << g_grouped_urls.size() << " domains)" << std::endl;
                } catch (const std::exception& e) {
                    std::cerr << "[WARNING] Could not parse existing JSON file: " << e.what() 
                              << ". Starting fresh." << std::endl;
                    g_grouped_urls.clear();
                    g_logged_urls.clear();
                }
                existing_json.close();
            } else {
                // File doesn't exist, will be created on first write
                std::cout << "[INFO] JSON output file does not exist, will create: " << g_json_path << std::endl;
            }
        }

        // Open TXT file in append mode to preserve existing data
        if (!txt_path.empty()) {
            // First, load existing URLs from TXT file into g_logged_urls
            std::ifstream existing_txt(txt_path);
            if (existing_txt.is_open()) {
                std::string line;
                int txt_urls_loaded = 0;
                while (std::getline(existing_txt, line)) {
                    if (!line.empty()) {
                        g_logged_urls.insert(line);
                        txt_urls_loaded++;
                    }
                }
                existing_txt.close();
                std::cout << "[INFO] Loaded " << txt_urls_loaded << " URLs from existing TXT file" << std::endl;
            }
            
            // Open in append mode
            txt_file.open(txt_path, std::ios::app);
            if (txt_file.is_open()) {
                std::cout << "[INFO] Opened TXT output file in append mode: " << txt_path << std::endl;
            } else {
                std::cerr << "[ERROR] Could not open file for writing: " << txt_path << std::endl;
            }
        }
    }

    void close_output_streams() {
        // JSON file is closed after every write in log_url, so nothing to do here for JSON.
        if (txt_file.is_open()) {
            txt_file.close();
        }
    }

	// This function logs a URL to both TXT and JSON outputs, ensuring no duplicates are logged.
    void log_url(const std::string& url_str) {
        // Check if URL was already logged (prevent duplicates)
        if (g_logged_urls.find(url_str) != g_logged_urls.end()) {
            return; // Already logged, skip
        }
        
        // Mark as logged
        g_logged_urls.insert(url_str);

        // TXT logging (Raw URL)
        if (txt_file.is_open()) {
            txt_file << url_str << "\n";
            txt_file.flush();
        }

        // JSON logging (Grouped by domain with paths only)
        if (!g_json_path.empty()) {
            // 1. Extract host and path
            std::string host = "unknown";
            std::string path = "/";
            try {
                auto r = boost::urls::parse_uri(url_str);
                if (r) {
                    host = r->host();
                    // Construct path from encoded_path + encoded_query
                    path = r->encoded_path();
                    if (!r->encoded_query().empty()) {
                        path += "?" + std::string(r->encoded_query());
                    }
                    if (path.empty()) {
                        path = "/";
                    }
                }
            } catch (...) {}

            // 2. Update memory state (store only path, not full URL)
            g_grouped_urls[host].insert(path);

            // 3. Rewrite file
            // Convert the map<string, unordered_set<string>> to map<string, vector<string>> for JSON
            std::map<std::string, std::vector<std::string>> json_data;
            for (const auto& [domain, paths] : g_grouped_urls) {
                json_data[domain] = std::vector<std::string>(paths.begin(), paths.end());
            }

            std::ofstream jf(g_json_path); 
            if (jf.is_open()) {
                nlohmann::json j(json_data);
                jf << j.dump(4);
            }
        }
    }

}
