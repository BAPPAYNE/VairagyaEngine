#include <string>
#include <iostream>
#include <atomic>
#include <csignal>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

#include "url/normalize.h"
#include "url/validate.h"
#include "url/process.h"
#include "crawler/engine.h"
#include "utils/log.h"
#include "net/fetcher.h"
#include "utils/runtime.h"
#include "utils/argparse.hpp"
#include "utils/utils.h"
#include "utils/config.h"
#include "utils/hash.h"

#include <boost/url.hpp>

#include "pipeline/doc_core_builder.h"

using namespace std;
using namespace argparse;

atomic<bool> g_running{ true };
ArgumentParser program("VairagyaEngine");

// Signal Handlers
#ifdef _WIN32
BOOL WINAPI consoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT) {
        cout << "\n[SHUTDOWN] Ctrl+C received\n";
        g_running = false;
        return TRUE;
    }
    return FALSE;
}
#endif

void handle_sigint(int) {
    cout << "\n[SHUTDOWN] SIGINT received\n";
    g_running = false;
}

// Utility Functions
inline const char* enum_to_string(net::FetchStatus status) {
    switch (status) {
        case net::FetchStatus::SUCCESS: return "SUCCESS";
        case net::FetchStatus::FAILED: return "FAILED";
        case net::FetchStatus::TIMEOUT: return "TIMEOUT";
        case net::FetchStatus::NOT_FOUND: return "NOT_FOUND";
        case net::FetchStatus::UNAUTHORIZED: return "UNAUTHORIZED";
        case net::FetchStatus::FORBIDDEN: return "FORBIDDEN";
        case net::FetchStatus::SERVER_ERROR: return "SERVER_ERROR";
        case net::FetchStatus::UNKNOWN_ERROR: return "UNKNOWN_ERROR";
        default: return "INVALID_STATUS";
    }
}

inline const char *enum_to_string(URLStatus status) {
    switch (status) {
        case URLStatus::INVALID_URL: return "INVALID";
        case URLStatus::RELATIVE_URL: return "RELATIVE";
        case URLStatus::DISALLOWED_URL: return "DISALLOWED";
        case URLStatus::ACCEPTED_URL: return "ACCEPTED";
        default: return "INVALID_STATUS";
    }
}

// Argument Parsing
inline void setupArgumentParser() {
    program.add_description("VairagyaEngine Web Crawler");
    
    program.add_argument("-l", "--list")
        .help("Specify list of url for initial urls crawling.")
        .default_value(string(""));

    program.add_argument("-cl", "--crawl-links")
        .help("Enable link extraction and recursive crawling.")
        .default_value(false)
        .implicit_value(true);

    program.add_argument("-v", "--verbose")
        .help("Enable verbose logging.")
        .default_value(false)
        .implicit_value(true);

    program.add_argument("-sd", "--same-domain")
        .help("Restrict crawling to the same domain as seed URLs.")
        .default_value(false)
        .implicit_value(true);

    program.add_argument("-oj", "--output-json")
        .help("JSON output file.")
        .default_value(string(""));

    program.add_argument("-o", "--output")
        .help("TXT output file.")
        .default_value(string(""));
}

inline int parseArguments(int &argc, char *argv[]) {
    setupArgumentParser();
    
    try {
        program.parse_args(argc, argv);
    }
    catch (const exception &err) {
        cerr << "[ERROR] Argument parsing failed: " << err.what() << endl;
        cerr << program;
        return 1;
    }
    return 0;
}

// Seed URL Loading
inline vector<string> loadSeedUrls() {
    vector<string> seedUrls;
    
    list_path = program.get<string>("--list");
    if (!list_path.empty()) {
        if (isValidPath(list_path)) {
            cout << "[INFO] Loading URLs from: " << list_path << endl;
            seedUrls = fetchLinesFromFile(list_path);
            if (seedUrls.empty()) {
                 cerr << "[WARNING] File is empty. Utilizing default seed URLs." << endl;
            }
        } else {
             cerr << "[ERROR] Invalid file path provided: " << list_path << endl;
             return {};  // Return empty vector to signal error
        }
    }

    // Default URLs if none loaded (or file was empty)
    if (seedUrls.empty()) {
        cout << "[INFO] Using default seed URLs." << endl;
        seedUrls = {
            //"https://stackoverflow.com/questions"
            "https://www.youtube.com"
        };
    }
    
    return seedUrls;
}

// Configuration Setup
inline void loadConfiguration() {
    crawl_links = program.get<bool>("--crawl-links");
    same_domain = program.get<bool>("--same-domain");
    json_output_path = program.get<string>("--output-json");
    txt_output_path = program.get<string>("--output");
}

inline void extractAllowedDomains(const vector<string>& seedUrls) {
    if (!same_domain) {
        return;  // Skip if same-domain mode is not enabled
    }
    
    for (const auto& url : seedUrls) {
        try {
            auto parsed = boost::urls::parse_uri(url);
            if (parsed) {
                string domain = string(parsed->host());
                allowed_domains.insert(domain);
            }
        } catch (...) {
            cerr << "[WARNING] Could not parse domain from: " << url << endl;
        }
    }
    
    // Display allowed domains
    cout << "[INFO] Same-domain mode enabled. Allowed domains: ";
    for (const auto& domain : allowed_domains) {
        cout << domain << " ";
    }
    cout << "\n";
}

// Display Configuration
inline void displayCrawlerInfo(const vector<string>& seedUrls) {
    cout << "[INFO] Starting Crawler with " << seedUrls.size() << " seed URLs.\n";
    cout << "[INFO] Link extraction enabled: " << (crawl_links ? "Yes" : "No") << "\n";
}

// Signal Handler Setup
inline void setupSignalHandlers() {
#ifdef _WIN32
    SetConsoleCtrlHandler(consoleHandler, TRUE);
#endif
    signal(SIGINT, handle_sigint);
}

int main(int argc, char *argv[])
{
    // Setup signal handlers for graceful shutdown
    setupSignalHandlers();
    
    if (parseArguments(argc, argv) != 0) {
        return 1;
    }

    // Load seed URLs from file or use defaults
    vector<string> seedUrls = loadSeedUrls();
    if (seedUrls.empty()) {
        return 1;  // Error already logged in loadSeedUrls
    }

    // Load configuration from parsed arguments
    loadConfiguration();

    // Extract allowed domains if same-domain mode is enabled
    extractAllowedDomains(seedUrls);

    displayCrawlerInfo(seedUrls);

    crawler::runCrawler(seedUrls);
    
    cout << "[EXIT] Crawler stopped cleanly\n";
 //   string input = R"()";
 //   cin >> input;
 //   string hash = DocCoreBuilder::get_canonical_url(input);
	//cout << "Canonical URL: " << hash << endl;
    
    return 0;
}

