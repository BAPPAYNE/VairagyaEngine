#ifndef NOMINMAX
#define NOMINMAX
#endif

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
#include "storage/rocksdb_store.h"
#include "pipeline/doc_core_builder.h"
#include <boost/url.hpp>

using namespace std;
using namespace argparse;

atomic<bool> g_running{ true };
ArgumentParser program("VairagyaEngine");

// Inline helper to initialize database schema and CFs
inline std::shared_ptr<storage::RocksDBStore> initDatabase(const std::string& db_path) {
    auto store = std::make_shared<storage::RocksDBStore>();
    if (!store->open(db_path)) {
        return nullptr;
    }
    return store;
}


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

    // Mutually exclusive group for input source
    auto& input_group = program.add_mutually_exclusive_group(true); // required

    input_group.add_argument("-d", "--domain")
        .help("Specify a single domain/URL to crawl.")
        .nargs(1);

    input_group.add_argument("-l", "--list")
        .help("Specify list of url for initial urls crawling.")
        .nargs(1);

    input_group.add_argument("-cd", "--crawl-database")
        .help("Crawl all URLs from the database as seeds.")
        .default_value(false)
        .implicit_value(true);

    input_group.add_argument("--resume-db")
        .help("Resume crawling from database frontier (load uncrawled/scheduled URLs from DB).")
        .default_value(false)
        .implicit_value(true);

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

    program.add_argument("-ir", "--ignore-robots")
        .help("Ignore robots.txt rules when fetching sites.")
        .default_value(false)
        .implicit_value(true);

    program.add_argument("-oj", "--output-json")
        .help("JSON output file.")
        .default_value(string(""));

    program.add_argument("-o", "--output")
        .help("TXT output file.")
        .default_value(string(""));

    program.add_argument("-db", "--database")
        .help("Path to the RocksDB database.")
        .default_value(string("vairagya_db"));

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
    
    // 1. Check for single domain argument first
    string single_domain = program.get<string>("--domain");
    if (!single_domain.empty()) {
        cout << "[INFO] Using single domain from CLI: " << single_domain << endl;
        seedUrls.push_back(single_domain);
        return seedUrls;
    }

    // 2. Check for list file
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
    ignore_robots = program.get<bool>("--ignore-robots");
    json_output_path = program.get<string>("--output-json");
    txt_output_path = program.get<string>("--output");
}

inline void extractAllowedDomains(const vector<string>& seedUrls) {
    if (!same_domain) {
        return;  // Skip if same-domain mode is not enabled
    }
    
    for (const auto& url : seedUrls) {
        std::string test_url = url;
        // Auto-prepend scheme if missing
        if (test_url.find("://") == std::string::npos) {
            test_url = "http://" + test_url;
        }
        try {
            auto parsed = boost::urls::parse_uri(test_url);
            if (parsed) {
                string domain = string(parsed->host());
                allowed_domains.insert(domain);
            } else {
                cerr << "[WARNING] Could not parse domain from: " << url << endl;
            }
        } catch (...) {
            cerr << "[WARNING] Exception parsing domain from: " << url << endl;
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

    // Load configuration from parsed arguments
    loadConfiguration();

    // Initialize database (schema and CFs)
    string db_path = program.get<string>("--database");
    auto db = initDatabase(db_path);
    if (!db) {
        cerr << "[ERROR] Database initialization failed: " << db_path << endl;
        return 1;
    }

    vector<string> seedUrls;
    if (program.get<bool>("--resume-db")) {
        seedUrls = db->loadPendingURLs();
        if (seedUrls.empty()) {
            cerr << "[ERROR] No pending URLs found in DB to resume." << endl;
            return 1;
        }
        cout << "[INFO] Resuming " << seedUrls.size() << " pending URLs from DB." << endl;
    } else if (program.get<bool>("--crawl-database")) {
        // Load all URLs from DB (implement db->getAllUrls() as needed)
        seedUrls = db->getUrlsBatch(100); // Example batch size
        if (seedUrls.empty()) {
            cerr << "[ERROR] No URLs found in DB to crawl." << endl;
            return 1;
        }
        cout << "[INFO] Loaded " << seedUrls.size() << " URLs from DB." << endl;
    } else if (program.is_used("--domain")) {
        string single_domain = program.get<string>("--domain");
        cout << "[INFO] Using single domain from CLI: " << single_domain << endl;
        seedUrls.push_back(single_domain);
    } else if (program.is_used("--list")) {
        string list_path = program.get<string>("--list");
        if (isValidPath(list_path)) {
            cout << "[INFO] Loading URLs from: " << list_path << endl;
            seedUrls = fetchLinesFromFile(list_path);
        } else {
            cerr << "[ERROR] Invalid file path provided: " << list_path << endl;
            return 1;
        }
    }

    // Extract allowed domains if same-domain mode is enabled
    extractAllowedDomains(seedUrls);
    displayCrawlerInfo(seedUrls);

    crawler::runCrawler(seedUrls, db);

    cout << "[EXIT] Crawler stopped cleanly\n";
    return 0;
}

