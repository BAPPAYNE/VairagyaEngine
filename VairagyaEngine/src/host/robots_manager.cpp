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

static bool startsWithIgnoreCase(const string& str, const string& prefix) {
    if (str.size() < prefix.size()) return false;
    return equal(prefix.begin(), prefix.end(), str.begin(), [](char a, char b) {
        return tolower((unsigned char)a) == tolower((unsigned char)b);
    });
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
        if (startsWithIgnoreCase(line, "User-agent:")) {

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
        if (startsWithIgnoreCase(line, "Sitemap:")) {
            string url = line.substr(8);
            trim(url);
            if (!url.empty())
                rules.sitemaps.emplace_back(url);
            continue;
        }

        // Rules (only if group applies)
        if (!group_matches)
            continue;

        if (startsWithIgnoreCase(line, "Allow:")) {
            string path = line.substr(6);
            trim(path);
            if (!path.empty())
                rules.allow.emplace_back(path);
            continue;
        }

        if (startsWithIgnoreCase(line, "Disallow:")) {
            string path = line.substr(9);
            trim(path);
            if (!path.empty())
                rules.disallow.emplace_back(path);
            continue;
        }
    }
    return rules;
}

void RobotsManager::absolutizeRobotsRules(const string& base_url, RobotsRules& rules) {
    auto make_absolute = [&](string& path) {
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
    lock_guard<mutex> lock(cache_mutex_);
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

string RobotsManager::getRobotsURL(const string& url) {
    auto scheme_end = url.find("://");
    if (scheme_end == string::npos) return ""; 

    auto host_start = scheme_end + 3;
    auto host_end = url.find('/', host_start);
    
    string origin;
    if (host_end == string::npos) {
        origin = url;
    } else {
        origin = url.substr(0, host_end);
    }
    return origin + "/robots.txt";
}

bool RobotsManager::hasRulesForUrl(const string& url) {
    string host = extractHost(url);
    if (host.empty()) return false;
    lock_guard<mutex> lock(cache_mutex_);
    return cache.find(host) != cache.end();
}


static bool matchesRule(const string& path, const string& pattern) {
    size_t p_len = pattern.length();
    size_t s_len = path.length();
    
    // Check for '$' at the end of the pattern
    bool end_anchor = (p_len > 0 && pattern.back() == '$');
    string effective_pattern = end_anchor ? pattern.substr(0, p_len - 1) : pattern;
    
    size_t p_idx = 0;
    size_t s_idx = 0;
    size_t star_idx = string::npos;
    size_t match_idx = 0;
    
    size_t ep_len = effective_pattern.length();

    while (s_idx < s_len) {
        if (p_idx < ep_len && effective_pattern[p_idx] == path[s_idx]) {
            p_idx++;
            s_idx++;
        }
        else if (p_idx < ep_len && effective_pattern[p_idx] == '*') {
            star_idx = p_idx;
            match_idx = s_idx;
            p_idx++;
        }
        else if (star_idx != string::npos) {
            p_idx = star_idx + 1;
            match_idx++;
            s_idx = match_idx;
        }
        else {
            return false; 
        }
    }

    // If we exhausted string, we must also exhaust pattern (handling trailing *)
    while (p_idx < ep_len && effective_pattern[p_idx] == '*') {
        p_idx++;
    }

    // If anchored, pattern must match whole string.
    // If not anchored, we only needed to match the prefix (which means we consumed enough of path to satisfy pattern)
    // BUT the loop above consumes the whole string.
    
    // WAIT. Robots.txt is a PREFIX match. The loop above enforces that the PATTERN matches the STRING.
    // If checking prefix:
    // /foo matches /foobar
    // The loop above will FAIL for /foobar vs /foo because p_idx hits end but s_idx < s_len.
    
    // Let's rewrite for Prefix Match support explicitly.
    return true; 
}

// Improved Wildcard Matcher for Robots.txt
static bool robotsMatch(const string& path, const string& rule) {
    size_t pathLen = path.length();
    size_t ruleLen = rule.length();
    
    size_t pi = 0; // path index
    size_t ri = 0; // rule index
    
    size_t starRi = string::npos;
    size_t starPi = string::npos;

    while (pi < pathLen && ri < ruleLen) {
        if (rule[ri] == '$' && ri == ruleLen - 1) {
             // End anchor reached. We must be at end of path (handled below).
             break;
        }
        if (rule[ri] == path[pi]) {
            pi++; ri++;
        }
        else if (rule[ri] == '*') {
            starRi = ri;
            starPi = pi;
            ri++;
        }
        else if (starRi != string::npos) {
             // Mismatch after *, backtrack
             ri = starRi + 1;
             starPi++;
             pi = starPi;
        }
        else {
            return false;
        }
    }

    // Handle trailing * in rule
    while (ri < ruleLen && rule[ri] == '*') ri++;
    
    // If we finished the rule:
    if (ri == ruleLen) {
        return true; // Match! (Prefix match successful)
    }

    // If rule ended with $, we must be at the end of path
    if (rule[ri] == '$' && ri == ruleLen - 1) {
        return pi == pathLen;
    }

    return false;
}


bool RobotsManager::canFetch(const string& url) {

    string host = extractHost(url);
    if (host.empty()) return false; 

    // Normalize empty path to "/"
    string path = extractPath(url);
    if (path.empty()) path = "/";

    // Optimization: If no entry, assume allowed (or maybe we should fetch?)
    // Current design assumes populate happens externally or lazily. 
    // If extraction happens on same domain, rules might be there.
    RobotsRules rules;
    {
        lock_guard<mutex> lock(cache_mutex_);
        auto it = cache.find(host);
        if (it == cache.end())
            return true;
        rules = it->second;
    }

    int best_allow_len = -1;
    int best_disallow_len = -1;

    for (const auto& rule : rules.allow) {
        if (robotsMatch(path, rule)) {
             if ((int)rule.length() > best_allow_len) best_allow_len = (int)rule.length();
        }
    }

    for (const auto& rule : rules.disallow) {
         if (robotsMatch(path, rule)) {
             if ((int)rule.length() > best_disallow_len) best_disallow_len = (int)rule.length();
        }
    }

    if (best_allow_len == -1 && best_disallow_len == -1) return true;
    
    // google/rfc: "The longest rule matches"
    return (best_allow_len >= best_disallow_len);
}
