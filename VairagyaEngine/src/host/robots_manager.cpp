#include "host/robots_manager.h"

#include <sstream>
#include <algorithm>


RobotsManager::RobotsManager(HostStateStore& hostStore)
    : hostStore_(hostStore) {}

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

// Lower-case copy helper.
static string toLowerCopy(string s) {
    transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return (char)tolower(c); });
    return s;
}

// A robots User-agent value matches our crawler if it is a case-insensitive
// prefix of our agent token (google/robotstxt rule: "Google" matches
// "Googlebot"). "*" is handled separately as the global group.
static bool agentApplies(const string& robots_agent, const string& our_agent) {
    string ra = toLowerCopy(robots_agent);
    string oa = toLowerCopy(our_agent);
    if (ra.empty()) return false;
    return oa.rfind(ra, 0) == 0;
}

// Parse robots.txt with correct group selection. Per google/robotstxt, a group
// that targets our specific agent OVERRIDES the wildcard "*" group entirely
// (they are NOT merged). We therefore accumulate rules into a "specific" bucket
// and a "global" bucket and return the specific one if our agent was named.
RobotsRules RobotsManager::extractRobotsDirectives(const string& robots_txt, const string& crawler_agent) {
    RobotsRules specific, global;

    istringstream iss(robots_txt);
    string line;

    bool collecting_agents = false; // currently inside a run of User-agent lines
    bool group_global = false;      // current group targets "*"
    bool group_specific = false;    // current group targets our agent
    bool seen_specific = false;     // our agent was named anywhere

    while (getline(iss, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        size_t hash = line.find('#');
        if (hash != string::npos)
            line.erase(hash);

        trim(line);
        if (line.empty())
            continue;

        if (startsWithIgnoreCase(line, "User-agent:")) {
            string agent = line.substr(11);
            trim(agent);

            // A directive since the last User-agent line starts a fresh group.
            if (!collecting_agents) {
                collecting_agents = true;
                group_global = group_specific = false;
            }

            if (agent == "*") {
                group_global = true;
            }
            else if (agentApplies(agent, crawler_agent)) {
                group_specific = true;
                seen_specific = true;
            }
            continue;
        }

        collecting_agents = false; // first non-User-agent line closes the run

        // Sitemaps are global (independent of groups).
        if (startsWithIgnoreCase(line, "Sitemap:")) {
            string url = line.substr(8);
            trim(url);
            if (!url.empty()) {
                specific.sitemaps.emplace_back(url);
                global.sitemaps.emplace_back(url);
            }
            continue;
        }

        if (startsWithIgnoreCase(line, "Allow:")) {
            string path = line.substr(6);
            trim(path);
            if (!path.empty()) {
                if (group_specific) specific.allow.emplace_back(path);
                if (group_global)   global.allow.emplace_back(path);
            }
            continue;
        }

        if (startsWithIgnoreCase(line, "Disallow:")) {
            string path = line.substr(9);
            trim(path);
            if (!path.empty()) {
                if (group_specific) specific.disallow.emplace_back(path);
                if (group_global)   global.disallow.emplace_back(path);
            }
            continue;
        }
    }

    // Specific group wins if our agent was named at all (even with no rules).
    return seen_specific ? specific : global;
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
    }
    else {
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


// Wildcard matcher for robots.txt: prefix match with '*' (any run) and a
// trailing '$' end anchor.
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