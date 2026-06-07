#ifndef RATE_LIMIT_H
#define RATE_LIMIT_H

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

using namespace std;

namespace host {

    class HostRateLimiter {
    public:
        explicit HostRateLimiter(uint32_t default_delay_ms = 1500);

        void waitTurn(const string& host);

        void markSuccess(const string& host);

        void markThrottled(const string& host, long long retry_after_ms);

    private:
        struct State {
            chrono::steady_clock::time_point next_allowed = chrono::steady_clock::now();
            uint32_t delay_ms = 1500;
            uint32_t penalty_ms = 0;
        };

        uint32_t default_delay_ms_;
        unordered_map<string, State> states_;
        mutex mutex_;
        condition_variable cv_;
    };

}

#endif