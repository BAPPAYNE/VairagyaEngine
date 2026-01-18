#include "host/robots_manager.h"

RobotsManager::RobotsManager(HostStateStore& hostStore)
	: hostStore_(hostStore) {
}

void RobotsManager::ensureRobots(const string& host, uint64_t now_ts) {
	auto stateOpt = hostStore_.get(host); // Check if we already have robots.txt info
	if (stateOpt.has_value()) {
		return; // Already have robots.txt info
	}

	HostState hostState;
	hostState.host = host;
	hostState.robots_allowed = true; // Default to allowed
	hostState.crawl_delay_ms = 1000;
	hostState.robots_fetch_ts = now_ts;
	hostState.last_request_ts = 0;

	hostStore_.put(hostState);
}

bool RobotsManager::isAllowed(const string& host, const string& path) {
	auto stateOpt = hostStore_.get(host);
	if (stateOpt.has_value()) {
		return stateOpt->robots_allowed;
	}
	return true;
}