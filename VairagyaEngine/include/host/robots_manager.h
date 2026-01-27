#ifndef ROBOTS_MANAGER
#define ROBOTS_MANAGER

#include "storage/host_state_store.h"

#include <string>
#include <cstdint>
#include <vector>
#include <unordered_map>

using namespace storage;

struct RobotsRules {
	vector<string> allow;
	vector<string> disallow;
	vector<string> sitemaps;
};

class RobotsManager {
	
public:
	explicit RobotsManager(HostStateStore& hostStore);
	// Ensure robots.txt has been fetched and parsed for the given host
	void ensureRobots(const string& host, uint64_t now_ts);
	// Check if the given path is allowed to be crawled on the host
	bool isAllowed(const string& host, const string& path);

	RobotsRules extractRobotsDirectives(const string& robots_txt, const string& crawler_agent);
	void absolutizeRobotsRules(const string& base_url, RobotsRules& directives_rules);
	bool canFetch(const string& url);
	void updateRobots(const string& host, const RobotsRules& rules);
private:
	HostStateStore& hostStore_;

	static string extractHost(const string& url);
	static string extractPath(const string& url);
	unordered_map<string, RobotsRules> cache;
};

#endif // ROBOTS_MANAGER
