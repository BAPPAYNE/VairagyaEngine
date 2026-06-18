#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <string>
#include <iostream>
#include <atomic>
#include <csignal>
#include <vector>
#include <algorithm>
#include <boost/url.hpp>
#ifdef _WIN32
#include <windows.h>
#endif

#include "url/normalize.hpp"
#include "url/validate.hpp"
#include "url/process.hpp"
#include "crawler/engine.hpp"
#include "utils/log.hpp"
#include "net/fetcher.hpp"
#include "utils/runtime.hpp"
#include "utils/argparse.hpp"
#include "utils/utils.hpp"
#include "utils/config.hpp"
#include "storage/rocksdb_store.hpp"
#include "pipeline/doc_core_builder.hpp"
#include "api/search_api.hpp"

using namespace std;
using namespace argparse;

atomic<bool> g_running{ true };
atomic<bool> g_shutdown_message_printed{ false };
mutex g_io_mtx;
ArgumentParser program("VairagyaEngine");

// Inline helper to initialize database schema and CFs
inline shared_ptr<storage::RocksDBStore> initDatabase(const string& db_path) {
    auto store = make_shared<storage::RocksDBStore>();
    if (!store->open(db_path)) {
        return nullptr;
    }
    return store;
}


// Signal Handlers
#ifdef _WIN32
BOOL WINAPI consoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT || signal == CTRL_BREAK_EVENT) {
        g_running.store(false);
        return TRUE;
    }
    return FALSE;
}
#endif

void handle_sigint(int) {
    g_running.store(false);
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

inline const char* enum_to_string(URLStatus status) {
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
    auto& input_group = program.add_mutually_exclusive_group(false);

    program.add_argument("--mode")
        .help("Runtime mode: crawler or api.")
        .default_value(string("crawler"));

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

    input_group.add_argument("--remove-duplicates")
        .help("Remove duplicate URL records and duplicate pending URLs from the RocksDB database, then exit.")
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

    program.add_argument("-t", "--threads")
        .help("Number of persistent crawler worker threads.")
        .default_value(1)
        .scan<'i', int>();

    program.add_argument("--port")
        .help("HTTP port for API mode.")
        .default_value(8080)
        .scan<'i', int>();

}

inline int parseArguments(int& argc, char* argv[]) {
    setupArgumentParser();

    try {
        if (argc <= 1) {
            cout << "[INFO] No CLI flags specified. Using default crawler args.\n";

            static vector<string> defaultArgs = {
                argv[0],
                "--mode", "crawler",
                "-d", "https://cplusplus.com/",
                "-db", "vairagya_db",
                "-t", "5",
                "-sd",
                "-cl"
            };

            vector<char*> defaultArgv;
            defaultArgv.reserve(defaultArgs.size());

            for (auto& arg : defaultArgs) {
                defaultArgv.push_back(arg.data());
            }

            int defaultArgc = static_cast<int>(defaultArgv.size());
            program.parse_args(defaultArgc, defaultArgv.data());
        }
        else {
            program.parse_args(argc, argv);
        }
    }
    catch (const exception& err) {
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
        }
        else {
            cerr << "[ERROR] Invalid file path provided: " << list_path << endl;
            return {};  // Return empty vector to signal error
        }
    }

    // Default URLs if none loaded (or file was empty)
    if (seedUrls.empty()) {
        cout << "[ERROR] No URL specified." << endl;
        exit(1);
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
        string test_url = url;
        // Auto-prepend scheme if missing
        if (test_url.find("://") == string::npos) {
            test_url = "http://" + test_url;
        }
        try {
            auto parsed = boost::urls::parse_uri(test_url);
            if (parsed) {
                string domain = string(parsed->host());
                allowed_domains.insert(domain);
            }
            else {
                cerr << "[WARNING] Could not parse domain from: " << url << endl;
            }
        }
        catch (...) {
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
    cout << "[INFO] Worker threads: " << max(1, program.get<int>("--threads")) << "\n";
}

// Signal Handler Setup
inline void setupSignalHandlers() {
#ifdef _WIN32
    SetConsoleCtrlHandler(consoleHandler, TRUE);
#else
    signal(SIGINT, handle_sigint);
#endif
}

int main(int argc, char* argv[])
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

    if (program.get<bool>("--remove-duplicates")) {
        auto stats = db->removeDuplicateURLs();
        cout << "[DEDUP] Removed duplicate document records: "
            << stats.duplicate_doc_records_removed << "\n";
        cout << "[DEDUP] Removed duplicate domain-index entries: "
            << stats.duplicate_domain_index_entries_removed << "\n";
        cout << "[DEDUP] Removed duplicate pending URLs: "
            << stats.duplicate_pending_urls_removed << "\n";
        if (stats.next_doc_id_repaired) {
            cout << "[DEDUP] Repaired next_doc_id: " << stats.next_doc_id << "\n";
        }
        cout << "[EXIT] Duplicate cleanup complete\n";
        return 0;
    }

    string mode = program.get<string>("--mode");
    if (mode == "api") {
        int requested_port = program.get<int>("--port");
        if (requested_port <= 0 || requested_port > 65535) {
            cerr << "[ERROR] Invalid API port: " << requested_port << endl;
            return 1;
        }

        uint16_t port = static_cast<uint16_t>(requested_port);
        api::runSearchApi(db, port);            
        return 0;
    }

    if (mode != "crawler") {
        cerr << "[ERROR] Invalid mode: " << mode << ". Use 'crawler' or 'api'." << endl;
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
    }
    else if (program.get<bool>("--crawl-database")) {
        seedUrls = db->getUrlsBatch(10000);
        if (seedUrls.empty()) {
            cout << "[INFO] No due recrawl URLs found. Falling back to stored document URLs." << endl;
            seedUrls = db->getAllDocumentUrls(10000);
        }
        if (seedUrls.empty()) {
            cerr << "[ERROR] No URLs found in DB to crawl." << endl;
            return 1;
        }
        cout << "[INFO] Loaded " << seedUrls.size() << " URLs from DB." << endl;
    }
    else if (program.is_used("--domain")) {
        string single_domain = program.get<string>("--domain");
        cout << "[INFO] Using single domain from CLI: " << single_domain << endl;
        seedUrls.push_back(single_domain);
    }
    else if (program.is_used("--list")) {
        string list_path = program.get<string>("--list");
        if (isValidPath(list_path)) {
            cout << "[INFO] Loading URLs from: " << list_path << endl;
            seedUrls = fetchLinesFromFile(list_path);
        }
        else {
            cerr << "[ERROR] Invalid file path provided: " << list_path << endl;
            return 1;
        }
    }

    if (seedUrls.empty()) {
        cerr << "[ERROR] No crawler input specified. Use --domain, --list, --crawl-database, --resume-db, or --mode api." << endl;
        return 1;
    }

    // Extract allowed domains if same-domain mode is enabled
    extractAllowedDomains(seedUrls);
    displayCrawlerInfo(seedUrls);

    int requested_threads = max(1, program.get<int>("--threads"));
    crawler::runCrawler(
        seedUrls,
        db,
        static_cast<size_t>(requested_threads),
        program.get<bool>("--resume-db")
    );

    cout << "[EXIT] Crawler stopped cleanly\n";
    return 0;
}
