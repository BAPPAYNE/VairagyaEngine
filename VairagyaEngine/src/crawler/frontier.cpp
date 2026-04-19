#include "crawler/frontier.h"
#include "url/process.h"

#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <vector>
#include <string>
#include <iostream>
#include <ctime>

using namespace std;

namespace crawler {

	// internal state for each host
	struct HostState {
		priority_queue<FrontierItem, vector<FrontierItem>, FrontierItemPriority> urlQueue;
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

	void Frontier::push(const string& inputURL, int depth, const string& referrer) {

		// 1. Validate + normalize
		ProcessedURL pURL = processURL(inputURL);
		if (pURL.status != URLStatus::ACCEPTED_URL) {
			cout << "[DROP] " << inputURL << " status=" << (int)pURL.status << "\n";
			return;
		}
		crawl_stats.discovered++;
		const string& url = pURL.normalized;

		// 2. Get or create URL state
		auto& state = urlStates[url];

		// Already saw this URL?
		if (!state.normalized_url.empty()) {
			// If it was already fetched, failed, or is already in the queue (UNKNOWN_ERROR), skip
			if (state.fetch_status == net::FetchStatus::SUCCESS ||
				state.fetch_status == net::FetchStatus::FAILED ||
				state.fetch_status == net::FetchStatus::ROBOTS_DISALLOWED ||
				state.fetch_status == net::FetchStatus::UNKNOWN_ERROR) {
				return;
			}
		}

		// First time seeing this URL or needs processing
		state.normalized_url = url;
		state.retry_count = 0;
		state.fetch_status = net::FetchStatus::UNKNOWN_ERROR; // Mark as pending
		state.http_status = 0;
		state.last_fetch_ts = 0;
		state.content_hash.clear();

		// 4. Retry limit reached → drop
		if (state.retry_count >= MAX_RETRY_COUNT) {
			return;
		}

		// 5. Extract host
		string host = extractHost(url);
		if (host.empty()) {
			return;
		}

		// 6. Enqueue
		hostQueue[host].urlQueue.push({
			url,
			pURL.priority,
			state.retry_count,
			depth,
			referrer
			});
	}



	optional<FrontierItem> Frontier::pop() {
		for (auto& [host, state] : hostQueue) {
			if (!state.urlQueue.empty()) {
				FrontierItem item = state.urlQueue.top();
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
		crawl_stats.fetched++;
	}



	void Frontier::markFailed(const string& url, uint16_t http_code) {
		auto& state = urlStates[url];

		state.fetch_status = net::FetchStatus::FAILED;
		state.http_status = http_code;
		state.last_fetch_ts = time(nullptr);
		crawl_stats.failed++;
	}



	void Frontier::markRetry(const string& url, net::FetchStatus fetch_status, uint16_t http_code)
	{
		auto& state = urlStates[url];

		state.fetch_status = fetch_status;
		state.http_status = http_code;
		state.retry_count++;
		state.last_fetch_ts = time(nullptr);
		if (state.retry_count < MAX_RETRY_COUNT) {
			crawl_stats.retried++;
			push(url);
		}
	}
	 
	void Frontier::markDisallowed(const string& url) {
		auto& state = urlStates[url];
		state.normalized_url = url;
		state.fetch_status = net::FetchStatus::ROBOTS_DISALLOWED;
		state.retry_count = 0;
		state.http_status = 0;
		state.last_fetch_ts = time(nullptr);
		crawl_stats.disallowed++;
	}

	vector<string> Frontier::getSuccessfulURLs() const {
		vector<string> successURLs;
		for (const auto& kv : urlStates) {
			const auto& state = kv.second;
			if (state.http_status == 200) {
				successURLs.push_back(state.normalized_url);
			}
		}
		return successURLs;
	}

	vector<string> Frontier::getPendingURLs() const {
		vector<string> pendingURLs;
		unordered_set<string> seen;
		for (const auto& [host, state] : hostQueue) {
			auto queueCopy = state.urlQueue;
			while (!queueCopy.empty()) {
				const auto& url = queueCopy.top().normalized_url;
				if (seen.insert(url).second) {
					pendingURLs.push_back(url);
				}
				queueCopy.pop();
			}
		}
		for (const auto& [url, state] : urlStates) {
			if (state.fetch_status == net::FetchStatus::UNKNOWN_ERROR && seen.insert(url).second) {
				pendingURLs.push_back(url);
			}
		}
		return pendingURLs;
	}
	
}
