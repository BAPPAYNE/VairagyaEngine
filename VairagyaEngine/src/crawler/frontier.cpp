#include "crawler/frontier.h"
#include "url/process.h"

#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <string>
#include <iostream>
#include<time.h>

using namespace std;

namespace crawler {

	// internal state for each host
	struct HostState {
		queue<FrontierItem> urlQueue;
	};

	// Simple hash function for URLs
	static size_t hashURL(const string& url) {
		return hash<string> {} (url);
	}

	static string extractHost(const string& url) {
		auto pos = url.find("://");
		if (pos == string::npos) {
			return "";
		}
		pos += 3; // Move past "://"
		auto end = url.find('/', pos);
		return url.substr(pos, end - pos);
	}

	void Frontier::push(const std::string& inputURL) {

		// 1. Validate + normalize
		ProcessedURL pURL = processURL(inputURL);
		if (pURL.status != URLStatus::ACCEPTED_URL) {
			cout << "[DROP] " << inputURL << " status=" << (int)pURL.status << "\n";
			return;
		}

		const std::string& url = pURL.normalized;

		// 2. Get or create URL state
		auto& state = urlStates[url];

		// First time seeing this URL
		if (state.normalized_url.empty()) {
			state.normalized_url = url;
			state.retry_count = 0;
			state.fetch_status = net::FetchStatus::UNKNOWN_ERROR; // initial placeholder
			state.http_status = 0;
			state.last_fetch_ts = 0;
			state.content_hash.clear();
		}

		// 3. Terminal success → never enqueue again
		if (state.fetch_status == net::FetchStatus::SUCCESS) {
			return;
		}

		// 4. Retry limit reached → drop
		if (state.retry_count >= MAX_RETRY_COUNT) {
			return;
		}

		// 5. Extract host
		std::string host = extractHost(url);
		if (host.empty()) {
			return;
		}

		// 6. Enqueue
		hostQueue[host].urlQueue.push({
			url,
			pURL.priority,
			state.retry_count
			});
	}



	optional<FrontierItem> Frontier::pop() {
		for (auto& [host, state] : hostQueue) {
			if (!state.urlQueue.empty()) {
				FrontierItem item = state.urlQueue.front();
				state.urlQueue.pop();
				return item;
			}
		}
		return nullopt;
	}

	bool Frontier::empty() const {
		for (auto& [host, state] : hostQueue) {
			if (!state.urlQueue.empty()) {
				return false;
			}
		}
		return true;
	}

	/*
	void Frontier::pushRetry(const FrontierItem& item) {
		if (item.retry_count >= MAX_RETRY_COUNT) {
			return;
		}
		FrontierItem retryItem = item;
		retryItem.retry_count += 1;
		retryItem.priority -= RETRY_PRIORITY_PENALTY;
		string host = extractHost(item.url);
		if (host.empty()) {
			return;
		}
		hostQueue[host].urlQueue.push(retryItem);

		return;
	}
	*/

	void Frontier::pushRetry(const FrontierItem& item) {
		string host = extractHost(item.normalized_url);
		if (host.empty()) {
			return;
		}

		hostQueue[host].urlQueue.push(item);
	}

	void Frontier::markFetched(const string& url, uint16_t http_code) {
		auto& state = urlStates[url];

		state.fetch_status = net::FetchStatus::SUCCESS;
		state.http_status = http_code;
		state.retry_count = 0;
		state.last_fetch_ts = time(nullptr);
	}



	void Frontier::markFailed(const string& url, uint16_t http_code) {
		auto& state = urlStates[url];

		state.fetch_status = net::FetchStatus::FAILED;
		state.http_status = http_code;
		state.last_fetch_ts = time(nullptr);
	}



	void Frontier::markRetry(const string& url, net::FetchStatus fetch_status, uint16_t http_code)
	{
		auto& state = urlStates[url];

		state.fetch_status = fetch_status;
		state.http_status = http_code;
		state.retry_count++;
		state.last_fetch_ts = time(nullptr);

		if (state.retry_count < MAX_RETRY_COUNT) {
			push(url);
		}
	}
}