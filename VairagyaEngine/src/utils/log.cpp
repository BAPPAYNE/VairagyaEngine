#include "utils/log.h"
#include <fstream>
#include <iostream>
#include <map>
#include <unordered_set>
#include <mutex>
#include <nlohmann/json.hpp>
#include <boost/url.hpp>

using namespace std;

void debug(const char* format, ...) {
#ifdef DEBUG
    va_list _ArgList;
    _crt_va_start(_ArgList, _Format);
    vprintf_s(_Format, _ArgList);
    _crt_va_end(_ArgList);
#endif
}

namespace log_utils {

    static string g_json_path;
    static ofstream txt_file;
    static map<string, unordered_set<string>> g_grouped_urls; // domain -> set of paths
    static unordered_set<string> g_logged_urls; // Track all logged URLs to prevent duplicates
    static mutex g_log_mutex;

    void init_output_streams(const string& json_path, const string& txt_path) {
        lock_guard<mutex> lock(g_log_mutex);
        g_json_path = json_path;

        // Load existing JSON data if file exists
        if (!g_json_path.empty()) {
            ifstream existing_json(g_json_path);
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
                                    string path_str = path.get<string>();
                                    g_grouped_urls[domain].insert(path_str);

                                    // Reconstruct full URL and add to logged URLs set
                                    string full_url = "https://" + domain + path_str;
                                    g_logged_urls.insert(full_url);
                                    total_urls_loaded++;
                                }
                            }
                        }
                    }

                    cout << "[INFO] Loaded existing JSON file: " << g_json_path
                        << " (" << total_urls_loaded << " URLs from "
                        << g_grouped_urls.size() << " domains)" << endl;
                }
                catch (const exception& e) {
                    cerr << "[WARNING] Could not parse existing JSON file: " << e.what()
                        << ". Starting fresh." << endl;
                    g_grouped_urls.clear();
                    g_logged_urls.clear();
                }
                existing_json.close();
            }
            else {
                // File doesn't exist, will be created on first write
                cout << "[INFO] JSON output file does not exist, will create: " << g_json_path << endl;
            }
        }

        // Open TXT file in append mode to preserve existing data
        if (!txt_path.empty()) {
            // First, load existing URLs from TXT file into g_logged_urls
            ifstream existing_txt(txt_path);
            if (existing_txt.is_open()) {
                string line;
                int txt_urls_loaded = 0;
                while (getline(existing_txt, line)) {
                    if (!line.empty()) {
                        g_logged_urls.insert(line);
                        txt_urls_loaded++;
                    }
                }
                existing_txt.close();
                cout << "[INFO] Loaded " << txt_urls_loaded << " URLs from existing TXT file" << endl;
            }

            // Open in append mode
            txt_file.open(txt_path, ios::app);
            if (txt_file.is_open()) {
                cout << "[INFO] Opened TXT output file in append mode: " << txt_path << endl;
            }
            else {
                cerr << "[ERROR] Could not open file for writing: " << txt_path << endl;
            }
        }
    }

    // Serializes the in-memory domain->paths map to the JSON file.
    // Caller must hold g_log_mutex.
    static void flushJsonLocked() {
        if (g_json_path.empty()) return;

        map<string, vector<string>> json_data;
        for (const auto& [domain, paths] : g_grouped_urls) {
            json_data[domain] = vector<string>(paths.begin(), paths.end());
        }

        ofstream jf(g_json_path);
        if (jf.is_open()) {
            nlohmann::json j(json_data);
            jf << j.dump(4);
        }
    }

    void close_output_streams() {
        lock_guard<mutex> lock(g_log_mutex);
        flushJsonLocked();   // final write (we no longer rewrite on every URL)
        if (txt_file.is_open()) {
            txt_file.close();
        }
    }

    // This function logs a URL to both TXT and JSON outputs, ensuring no duplicates are logged.
    void log_url(const string& url_str) {
        lock_guard<mutex> lock(g_log_mutex);
        // Check if URL was already logged (prevent duplicates)
        if (g_logged_urls.find(url_str) != g_logged_urls.end()) {
            return; // Already logged, skip
        }

        // Mark as logged
        g_logged_urls.insert(url_str);

        // TXT logging (Raw URL) - cheap append, always immediate.
        if (txt_file.is_open()) {
            txt_file << url_str << "\n";
            txt_file.flush();
        }

        // JSON logging (Grouped by domain with paths only)
        if (!g_json_path.empty()) {
            // 1. Extract host and path
            string host = "unknown";
            string path = "/";
            try {
                auto r = boost::urls::parse_uri(url_str);
                if (r) {
                    host = r->host();
                    // Construct path from encoded_path + encoded_query
                    path = r->encoded_path();
                    if (!r->encoded_query().empty()) {
                        path += "?" + string(r->encoded_query());
                    }
                    if (path.empty()) {
                        path = "/";
                    }
                }
            }
            catch (...) {}

            // 2. Update memory state (store only path, not full URL)
            g_grouped_urls[host].insert(path);

            // 3. Rewrite the file only periodically. Rewriting the entire JSON on
            //    every URL was O(n^2) I/O over a crawl; close_output_streams()
            //    performs the final flush so nothing is lost on clean exit.
            static size_t dirty = 0;
            if (++dirty % 64 == 0) {
                flushJsonLocked();
            }
        }
    }

}