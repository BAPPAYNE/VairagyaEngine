#include "crawler/frontier.h"
#include "url/process.h"

#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <string>

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

	void Frontier::push(const string& inputURL) {
		
		// 1. Process URL (vaidate + normalize)
		ProcessedURL pURL = processURL(inputURL);
		
		// 2. Check URL status
		if (pURL.status != URLStatus::ACCEPTED) {
			return;
		}

		// 3. Deduplicate
		size_t urlHash = hashURL(pURL.normalized);
		if (visitedURLs.find(urlHash) != visitedURLs.end()) {
			return; 
		}

		// 4. Extract Host
		string host = extractHost(pURL.normalized);
		if (host.empty()) {
			return;
		}

		// 5. Enqueue
		//FrontierItem item{url, priority, retry_count};
		hostQueue[host].urlQueue.push({ pURL.normalized, 0, 0 });

		// 6. Mark as visited
		visitedURLs.insert(urlHash);
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
		string host = extractHost(item.url);
		if (host.empty()) {
			return;
		}

		hostQueue[host].urlQueue.push(item);
	}
}