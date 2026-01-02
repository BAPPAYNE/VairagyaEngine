#include <boost/url.hpp>
#include <boost/url/url_view.hpp>
#include <boost/algorithm/string.hpp>

#include "url/validate.h"

using namespace boost::urls;
using namespace boost::algorithm;
using namespace std;


URLStatus analyzeURL(string urlInput, string* normalized) {
    trim(urlInput);

    auto parsedURL = parse_uri_reference(urlInput);
    if (!parsedURL) {
        return URLStatus::INVALID;
    }

    const auto& uri = parsedURL.value();

    if (!uri.has_scheme() || !uri.has_authority()) {
        return URLStatus::RELATIVE;
    }

    if (uri.scheme() != "http" && uri.scheme() != "https") {
        return URLStatus::DISALLOWED;
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

    return URLStatus::ACCEPTED;
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