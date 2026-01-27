#include "host/robots_manager.h"

#include <sstream>
#include <algorithm>


RobotsManager::RobotsManager(HostStateStore& hostStore)
	: hostStore_(hostStore) { }

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

static inline void trim(string& s) {
    s.erase(s.begin(), find_if(s.begin(), s.end(), [](unsigned char c) { return !isspace(c); }));
    s.erase(find_if(s.rbegin(), s.rend(), [](unsigned char c) { return !isspace(c); }).base(), s.end());
}

static inline bool startsWith(const string& s, const string& p) {
    return s.size() >= p.size() && equal(p.begin(), p.end(), s.begin());
}

RobotsRules RobotsManager::extractRobotsDirectives(const string& robots_txt, const string& crawler_agent) {
    RobotsRules rules;

    istringstream iss(robots_txt);
    string line;

    // Group state
    bool collecting_agents = false;   // still reading User-agent lines
    bool group_matches = false;        // does this group apply to us?

    while (getline(iss, line)) {

        // Handle CRLF
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        // Remove comments
        size_t hash = line.find('#');
        if (hash != string::npos)
            line.erase(hash);

        trim(line);
        if (line.empty())
            continue;

        // User-agent
        if (startsWith(line, "User-agent:")) {

            string agent = line.substr(11);
            trim(agent);

            // If we already passed rules, this starts a NEW group
            if (!collecting_agents) {
                collecting_agents = true;
                group_matches = false;
            }

            if (agent == "*" || agent == crawler_agent)
                group_matches = true;

            continue;
        }

        // First non-User-agent line ends agent collection
        collecting_agents = false;

        // Sitemap (global)
        if (startsWith(line, "Sitemap:")) {
            string url = line.substr(8);
            trim(url);
            if (!url.empty())
                rules.sitemaps.push_back(url);
            continue;
        }

        // Rules (only if group applies)
        if (!group_matches)
            continue;

        if (startsWith(line, "Allow:")) {
            string path = line.substr(6);
            trim(path);
            if (!path.empty())
                rules.allow.push_back(path);
            continue;
        }

        if (startsWith(line, "Disallow:")) {
            string path = line.substr(9);
            trim(path);
            if (!path.empty())
                rules.disallow.push_back(path);
            continue;
        }
    }
    return rules;
}

void RobotsManager::absolutizeRobotsRules(const std::string& base_url, RobotsRules& rules) {
    auto make_absolute = [&](std::string& path) {
        if (path.empty()) { return; }

        if (path.starts_with("http://") || path.starts_with("https://")) {
            return;
        }

        if (path.front() == '/') {
            path = base_url + path.substr(1);
        }
        else {
            path = base_url + path;
        }
        };

    for (auto& p : rules.allow) {
        make_absolute(p);
    }

    for (auto& p : rules.disallow) {
        make_absolute(p);
    }
}

void RobotsManager::updateRobots(const string& host, const RobotsRules& rules) {
	cache[host] = rules;
}

string RobotsManager::extractHost(const string& url) {
    auto scheme_end = url.find("://");
    if (scheme_end == string::npos) return "";

    auto host_start = scheme_end + 3;
    auto host_end = url.find('/', host_start);
    if (host_end == string::npos)
        return url.substr(host_start);

    return url.substr(host_start, host_end - host_start);
}

string RobotsManager::extractPath(const string& url) {
    auto scheme_end = url.find("://");
    if (scheme_end == string::npos) return "/";

    auto path_start = url.find('/', scheme_end + 3);
    if (path_start == string::npos)
        return "/";

    return url.substr(path_start);
}

bool RobotsManager::canFetch(const string& url) {

    string host = extractHost(url);
    if (host.empty())
        return false; // malformed URL → deny defensively

    auto it = cache.find(host);
    if (it == cache.end())
        return true;

    const RobotsRules& rules = it->second;
    string path = extractPath(url);

    int best_allow_len = -1;
    int best_disallow_len = -1;

    for (const auto& rule : rules.allow) {
        if (rule == "/") {
            best_allow_len = max(best_allow_len, 1);
        }
        else if (path.starts_with(rule)) {
            best_allow_len = max(best_allow_len, (int)rule.size());
        }
    }

    for (const auto& rule : rules.disallow) {
        if (rule == "/") {
            best_disallow_len = max(best_disallow_len, 1);
        }
        else if (path.starts_with(rule)) {
            best_disallow_len = max(best_disallow_len, (int)rule.size());
        }
    }

    if (best_allow_len < 0 && best_disallow_len < 0)
        return true;

    if (best_allow_len >= best_disallow_len)
        return true;

    return false;
}