#include <boost/url.hpp>
#include <boost/url/url_view.hpp>
#include <boost/algorithm/string.hpp>

#include "url/validate.hpp"

using namespace boost::urls;
using namespace boost::algorithm;
using namespace std;


constexpr int MAX_PRIORITY = 100;


URLStatus analyzeURL(string urlInput, string* normalized) {
    trim(urlInput);

    auto parsedURL = parse_uri_reference(urlInput);
    if (!parsedURL) {
        return URLStatus::INVALID_URL;
    }

    const auto& uri = parsedURL.value();

    if (!uri.has_scheme() || !uri.has_authority()) {
        return URLStatus::RELATIVE_URL;
    }

    if (uri.scheme() != "http" && uri.scheme() != "https") {
        return URLStatus::DISALLOWED_URL;
    }

    if (normalized) {
        boost::urls::url u(urlInput);
        u.normalize();

        if ((u.scheme() == "http" && u.port_number() == 80) ||
            (u.scheme() == "https" && u.port_number() == 443)) {
            u.remove_port();
        }

        *normalized = string(u.buffer());
    }

    return URLStatus::ACCEPTED_URL;
}

SchemeType extractScheme(const string& url) {
    auto pos = url.find("://");
    if (pos == string::npos) {
        return SchemeType::NONE;
    }
    string scheme = url.substr(0, pos);
	transform(scheme.begin(), scheme.end(), scheme.begin(), ::tolower);

    static const unordered_map<string, SchemeType> scheme_map = {
        {"http", SchemeType::HTTP},
        {"https", SchemeType::HTTPS},
        {"ftp", SchemeType::FTP},
        {"mailto", SchemeType::MAILTO},
        {"tel", SchemeType::TEL},
        {"scp", SchemeType::SCP},
        {"mqtt", SchemeType::MQTT},
        {"mqtts", SchemeType::MQTTS}
    };

    auto it = scheme_map.find(scheme);
	return it != scheme_map.end() ? it->second : SchemeType::UNKNOWN;
}

Crawlability assessCrawlability(SchemeType& scheme) {
    if (scheme == SchemeType::HTTP || scheme == SchemeType::HTTPS) {
        return Crawlability::CRAWLABLE;
    } else {
        return Crawlability::NON_CRAWLABLE;
	}
}

static string toLowerCopy(string s) {
    transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) {
            return static_cast<char>(tolower(c));
        }
    );

    return s;
}

static string stripQueryAndFragment(string url) {
    size_t fragmentPos = url.find('#');
    if (fragmentPos != string::npos) {
        url = url.substr(0, fragmentPos);
    }

    size_t queryPos = url.find('?');
    if (queryPos != string::npos) {
        url = url.substr(0, queryPos);
    }

    return url;
}

static bool endsWithAny(const string& s, initializer_list<string> suffixes) {
    for (const auto& suffix : suffixes) {
        if (s.size() >= suffix.size() &&
            equal(suffix.rbegin(), suffix.rend(), s.rbegin())) {
            return true;
        }
    }

    return false;
}

ResourceType classifyResourceType(const string& url) {
    string clean = stripQueryAndFragment(toLowerCopy(url));

    if (endsWithAny(clean, {
        ".html", ".htm", ".php", ".asp", ".aspx"
        })) {
        return ResourceType::HTML;
    }

    if (endsWithAny(clean, {
        ".txt", ".md", ".rst"
        })) {
        return ResourceType::TEXT_DOCUMENT;
    }

    if (endsWithAny(clean, {
        ".pdf"
        })) {
        return ResourceType::PDF_DOCUMENT;
    }

    if (endsWithAny(clean, {
        ".jpg", ".jpeg", ".png", ".webp", ".gif", ".svg", ".bmp", ".ico"
        })) {
        return ResourceType::IMAGE;
    }

    if (endsWithAny(clean, {
        ".mp4", ".webm", ".mov", ".avi", ".mkv", ".m4v"
        })) {
        return ResourceType::VIDEO;
    }

    if (endsWithAny(clean, {
        ".mp3", ".wav", ".ogg", ".flac", ".m4a", ".aac"
        })) {
        return ResourceType::AUDIO;
    }

    if (endsWithAny(clean, {
        ".css", ".js", ".mjs", ".json", ".xml",
        ".woff", ".woff2", ".ttf", ".eot",
        ".zip", ".rar", ".7z", ".tar", ".gz",
        ".exe", ".dll", ".bin", ".iso"
        })) {
        return ResourceType::STATIC_ASSET;
    }

    // URLs like:
    // https://learn.microsoft.com/en-us/cpp/
    // https://cplusplus.com/reference/vector/vector/
    // https://www.rfc-editor.org/info/rfc1264/
    return ResourceType::HTML;
}


static string getPathFromURL(const string& url) {
    size_t schemePos = url.find("://");
    if (schemePos == string::npos) {
        return "/";
    }

    size_t pathStart = url.find('/', schemePos + 3); // +3 to skip "://"


    if (pathStart == string::npos) {
        return "/";
    }

    string path = url.substr(pathStart);
    size_t queryPos = path.find('?');
    if (queryPos != string::npos) {
        path = path.substr(0, queryPos);
    }

    size_t fragmentPos = path.find('#');
    if (fragmentPos != string::npos) {
        path = path.substr(0, fragmentPos);
    }

    if (path.empty()) {
        return "/";
    }

    return path;
}


static bool isStaticAsset(const string& path) {
    // Check if the path ends with common static asset extensions
    return endsWithAny(path, {
        ".css", ".js", ".png", ".jpg", ".jpeg", ".svg", ".gif",
        ".webp", ".ico", ".woff", ".woff2", ".ttf", ".eot",
        ".mp4", ".mp3", ".avi", ".mov", ".zip", ".rar", ".7z",
        ".pdf"
        });
}

static bool looksLikeHtmlPage(const string& path) {
    if (path == "/") {
        return true;
    }

    if (path.ends_with("/")) {
        return true;
    }

    return endsWithAny(path, {
        ".html", ".htm", ".php", ".asp", ".aspx"
        });
}

static bool commonLowValuePatterns(const string& path) {
    // Check for patterns that often indicate low-value pages
    return
        path.find("/tag/") != string::npos ||
        path.find("/category/") != string::npos ||
        path.find("/archive/") != string::npos ||
        path.find("/search/") != string::npos ||
        path.find("/tags/") != string::npos ||
        path.find("/cart/") != string::npos ||
        path.find("/account/") != string::npos;
}

uint16_t priorityScore(const string& url) {
    string path = getPathFromURL(url);

    if (isStaticAsset(path)) {
        return 0; // Static assets get lowest priority
    }

    int p = MAX_PRIORITY / 2; // base score = 50

    uint8_t depth = 0;

    if (path != "/") {
        depth = static_cast<uint8_t>(count(path.begin(), path.end(), '/'));

        if (path.ends_with("/")) {
            depth--;
        }

        depth = max((uint8_t)0, depth);
    }

    p -= depth * MAX_PRIORITY / 20; // here MAX_PRIORITY/20 = 5 

    if (looksLikeHtmlPage(path)) {
        p += MAX_PRIORITY / 10;
    }

    // Prefer homepage strongly
    if (path == "/") {
        p += MAX_PRIORITY / 4; // here MAX_PRIORITY/4 = 25
    }

    // Penalize query-heavy URLs
    if (url.find('?') != string::npos) {
        p -= MAX_PRIORITY / 7; // here MAX_PRIORITY/7 = 14(int)
    }

    if (commonLowValuePatterns(path)) {
        p -= MAX_PRIORITY / 5; // here MAX_PRIORITY/5 = 20
    }

    if (p < 0) {
        p = 0;
    }

    if (p > MAX_PRIORITY) {
        p = MAX_PRIORITY;
    }

    return p;
}