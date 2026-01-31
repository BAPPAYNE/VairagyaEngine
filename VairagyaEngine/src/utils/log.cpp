#include "utils/log.h"
#include <fstream>
#include <iostream>
#include <map>
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
    static std::map<std::string, std::vector<std::string>> g_grouped_urls;

    void init_output_streams(const std::string& json_path, const std::string& txt_path) {
        g_json_path = json_path;
        if (!g_json_path.empty()) {
             // Clear file initially or load existing? 
             // Logic: If crawling starts fresh, we probably want a fresh file.
             // We will create/clear it now to ensure it exists.
             std::ofstream ofs(g_json_path);
             if (ofs.is_open()) {
                 ofs << "{}\n";
                 std::cout << "[INFO] Initialized JSON output file: " << g_json_path << std::endl;
             } else {
                 std::cerr << "[ERROR] Could not initialize JSON file: " << g_json_path << std::endl;
             }
        }

        if (!txt_path.empty()) {
            txt_file.open(txt_path);
            if (txt_file.is_open()) {
                std::cout << "[INFO] Opened TXT output file: " << txt_path << std::endl;
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

    void log_url(const std::string& url_str) {
        // TXT logging (Raw URL)
        if (txt_file.is_open()) {
            txt_file << url_str << "\n";
            txt_file.flush();
        }

        // JSON logging (Grouped by Host)
        if (!g_json_path.empty()) {
            // 1. Extract host
            std::string host = "unknown";
            try {
                auto r = boost::urls::parse_uri(url_str);
                if (r) {
                    host = r->host();
                }
            } catch (...) {}

            // 2. Update memory state
            g_grouped_urls[host].push_back(url_str);

            // 3. Rewrite file
            // Note: This is O(N) writes per URL, which is performance heavy but satisfies the structure requirement.
            std::ofstream jf(g_json_path); 
            if (jf.is_open()) {
                nlohmann::json j(g_grouped_urls);
                jf << j.dump(4);
            }
        }
    }

}
