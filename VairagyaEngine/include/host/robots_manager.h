#ifndef ROBOTS_MANAGER
#define ROBOTS_MANAGER

#include "storage/host_state_store.h"

#include <string>
#include <cstdint>

using namespace storage;

class RobotsManager {
public:
	explicit RobotsManager(HostStateStore& hostStore);
	// Ensure robots.txt has been fetched and parsed for the given host
	void ensureRobots(const string& host, uint64_t now_ts);
	// Check if the given path is allowed to be crawled on the host
	bool isAllowed(const string& host, const string& path);
private:
	HostStateStore& hostStore_;
};

#endif // ROBOTS_MANAGER
