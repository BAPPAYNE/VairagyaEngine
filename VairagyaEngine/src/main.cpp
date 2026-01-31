// GurenEngine.cpp : Defines the entry point for the application.
//

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

using namespace std;
using namespace argparse;

atomic<bool> g_running{ true };
ArgumentParser program("VairagyaEngine");

#ifdef _WIN32
#include <windows.h>


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


inline int handleArgument(int &argc, char *argv[]) {
    program.add_description("VairagyaEngine Web Crawler");
    program.add_argument("-l", "--list")
        .help("Specify list of url for initial urls crawling.")
        .default_value(string("")); // Default to empty string if not provided

    program.add_argument("-cl", "--crawl-links")
        .help("Enable link extraction and recursive crawling.")
        .default_value(false)
        .implicit_value(true);

    try {
        program.parse_args(argc, argv);
    }
    catch (const exception &err) {
        cerr << "[ERROR] Argument parsing failed: " << err.what() << endl;
        cerr << program;
        return 1;
    }
    return 0; // Success
}   

int main(int argc, char *argv[])
{
#ifdef _WIN32
    SetConsoleCtrlHandler(consoleHandler, TRUE);
#endif
    
    if (handleArgument(argc, argv) != 0) {
        return 1;
    }

    signal(SIGINT, handle_sigint);

    vector<string> seedUrls;

    // Check if -l flag is used and has a value
    auto listPath = program.get<string>("--list");
    if (!listPath.empty()) {
        if (isValidPath(listPath)) {
            cout << "[INFO] Loading URLs from: " << listPath << endl;
            seedUrls = fetchLinesFromFile(listPath);
            if (seedUrls.empty()) {
                 cerr << "[WARNING] File is empty. Utilizing default seed URLs." << endl;
            }
        } else {
             cerr << "[ERROR] Invalid file path provided: " << listPath << endl;
             return 1;
        }
    }

    // Default URLs if none loaded (or file was empty)
    if (seedUrls.empty()) {
        cout << "[INFO] Using default seed URLs." << endl;
        seedUrls = {
            "https://stackoverflow.com/questions"
        };
    }

    cout << "[INFO] Starting Crawler with " << seedUrls.size() << " seed URLs.\n";

    // Check if --crawl-links flag is used
    bool crawlLinks = program.get<bool>("--crawl-links");

    cout << "[INFO] Starting Crawler with " << seedUrls.size() << " seed URLs.\n";
    cout << "[INFO] Link extraction enabled: " << (crawlLinks ? "Yes" : "No") << "\n";

    crawler::runCrawler(seedUrls, crawlLinks);
    
    cout << "[EXIT] Crawler stopped cleanly\n";
    
    return 0;
}
