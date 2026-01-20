// GurenEngine.cpp : Defines the entry point for the application.
//

#include <string>
#include <iostream>
#include <atomic>
#include <csignal>

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

using namespace std;
atomic<bool> g_running{ true };

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

int main()
{
#ifdef _WIN32
    SetConsoleCtrlHandler(consoleHandler, TRUE);
#endif
    signal(SIGINT, handle_sigint);
    crawler::runCrawler();
    cout << "[EXIT] Crawler stopped cleanly\n";
    
    return 0;
}
