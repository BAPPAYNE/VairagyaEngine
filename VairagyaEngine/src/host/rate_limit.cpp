#include "host/rate_limit.hpp"

#include <algorithm>

using namespace std;

namespace host {

    HostRateLimiter::HostRateLimiter(uint32_t default_delay_ms)
        : default_delay_ms_(default_delay_ms) {}

    void HostRateLimiter::waitTurn(const string& host) {
        if (host.empty()) {
            return;
        }

        unique_lock<mutex> lock(mutex_);

        auto& state = states_[host];

        if (state.delay_ms == 0) {
            state.delay_ms = default_delay_ms_;
        }

        while (true) {
            auto now = chrono::steady_clock::now();

            if (now >= state.next_allowed) {
                uint32_t effective_delay = state.delay_ms + state.penalty_ms;

                state.next_allowed = now + chrono::milliseconds(effective_delay);

                return;
            }

            cv_.wait_until(lock, state.next_allowed);
        }
    }

    void HostRateLimiter::markSuccess(const string& host) {
        if (host.empty()) {
            return;
        }

        lock_guard<mutex> lock(mutex_);

        auto& state = states_[host];

        if (state.delay_ms == 0) {
            state.delay_ms = default_delay_ms_;
        }

        // Slowly reduce penalty after successful requests
        state.penalty_ms = state.penalty_ms / 2;

        cv_.notify_all();
    }

    void HostRateLimiter::markThrottled(const string& host, long long retry_after_ms) {
        if (host.empty()) {
            return;
        }

        lock_guard<mutex> lock(mutex_);

        auto& state = states_[host];

        if (state.delay_ms == 0) {
            state.delay_ms = default_delay_ms_;
        }

        constexpr uint32_t MIN_429_BACKOFF_MS = 10000;   // 10 seconds
        constexpr uint32_t MAX_429_BACKOFF_MS = 300000;  // 5 minutes

        uint32_t backoff_ms = MIN_429_BACKOFF_MS;

        if (retry_after_ms > 0) {
            backoff_ms = static_cast<uint32_t>(
                min<long long>(retry_after_ms, MAX_429_BACKOFF_MS)
                );
        }
        else {
            uint32_t previous =
                state.penalty_ms == 0
                ? MIN_429_BACKOFF_MS
                : state.penalty_ms * 2;

            backoff_ms = min(previous, MAX_429_BACKOFF_MS);
        }

        state.penalty_ms = backoff_ms;
        state.next_allowed = chrono::steady_clock::now() + chrono::milliseconds(backoff_ms);

        cv_.notify_all();
    }

}