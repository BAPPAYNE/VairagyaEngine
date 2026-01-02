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

#include "url/process.h"

using namespace std;

namespace crawler {

	struct FrontierItem {
		string url;
		int priority;
		int retry_count;
	};

	class Frontier {

	public:
		void push(const string& url);
		optional<FrontierItem> pop();
		bool empty() const;
		void pushRetry(const FrontierItem& url);

	private:
		struct HostState {
			queue<FrontierItem> urlQueue; // Queue of URLs for this host
		};
		unordered_map<string, HostState> hostQueue; // Map of host to its state
		unordered_set<size_t> visitedURLs; // Set of visited URL hashes
	};
};

#endif // FRONTIER_H