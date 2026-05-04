/*
	Frontier is a class that manages a queue of URLs to be crawled, ensuring that URLs are processed in a controlled manner, respecting host-based queuing and deduplication.
*/

#ifndef FRONTIER_H
#define FRONTIER_H

#include <string>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <vector>
#include <atomic>
#include <condition_variable>
#include <mutex>

#include "url/process.h"
#include "storage/url_state.h"

using namespace std;

namespace crawler {

	enum class CrawlStatus {
		NEW,
		FETCHED,
		FAILED,
		RETRYING
	};

	struct CrawlStats {
		uint64_t fetched = 0;
		uint64_t failed = 0;
		uint64_t retried = 0;
		uint64_t disallowed = 0;
		uint64_t discovered = 0;
	};

	struct FrontierItem {
		string normalized_url;
		int priority;
		uint8_t  retry_count;
		int depth;
		string referrer_url;
	};

	struct FrontierItemPriority {
		bool operator()(const FrontierItem& left, const FrontierItem& right) const {
			return left.priority < right.priority;
		}
	};

	class Frontier {

	public:
		void markFetched(const string& url, uint16_t http_status);
		void markFailed(const string& url, uint16_t http_status);
		void markRetry(const string& url, net::FetchStatus fetch_status, uint16_t http_status);
		void markDisallowed(const string& url);
		void push(const string& url, int depth = 0, const string& referrer = "");
		optional<FrontierItem> pop();
		optional<FrontierItem> popWait(const atomic<bool>& running_flag);
		bool empty() const;
		void pushRetry(const FrontierItem& url);
		void completeWork();
		void shutdown();
		CrawlStats stats() const;

		CrawlStats crawl_stats;

		vector<string> getSuccessfulURLs() const;
		vector<string> getPendingURLs() const;

	private:
		struct HostState {
			priority_queue<FrontierItem, vector<FrontierItem>, FrontierItemPriority> urlQueue;
		};
		unordered_map<string, HostState> hostQueue; // Map of host to its state
		//unordered_set<size_t> visitedURLs; // Set of visited URL hashes
		unordered_map<string, storage::URLState> urlStates;
		mutable mutex mutex_;
		condition_variable cv_;
		size_t active_workers_ = 0;
		bool accepting_work_ = true;

		bool hasQueuedURLLocked() const;
		optional<FrontierItem> popLocked();
	};
};

#endif // FRONTIER_H
