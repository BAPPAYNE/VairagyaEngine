#include "url/process.h"
#include "url/validate.h"
#include "url/normalize.h"

#include <boost/url.hpp>

using namespace std;
using namespace boost::urls;

constexpr int MAX_PRIORITY = 100;

int priorityScore(const string& url) {
	int p = MAX_PRIORITY/2; // base score = 50

	int depth = max(0, static_cast<int>(count(url.begin(), url.end(), '/') - 2)); // -2 to ignore '<protocol>://'
	p -= depth * MAX_PRIORITY/20; // here MAX_PRIORITY/20 = 5 
	
	if (url.find('.') == string::npos || url.ends_with(".html") || url.ends_with("/")) {
		p += MAX_PRIORITY/10; // here MAX_PRIORITY/10 = 10 
	}
	
	if (url.ends_with(".css") || url.ends_with(".js") || url.ends_with(".png") || url.ends_with(".jpg") || url.ends_with(".svg")) {
		p -= MAX_PRIORITY/5; // MAX_PRIORITY/5 = 20
	}

	if (p < 0) {
		p = 0;
	}

	if (p > MAX_PRIORITY) {
		p = MAX_PRIORITY;
	}

	return p;
}

ProcessedURL processURL(const string& input) {
	ProcessedURL out;
	out.original = input;

	auto normalized = normalizeURI(input);
	if (!normalized) {
		out.status = URLStatus::INVALID; 
		out.priority = 0;
		return out;
	}

	out.normalized = *normalized;
	out.status = analyzeURL(out.normalized);
	out.scheme = extractScheme(out.normalized);
	out.crawlability = assessCrawlability(out.scheme);

	if (out.crawlability == Crawlability::NON_CRAWLABLE) {
		out.priority = 0;
		return out;
	}
	out.priority = priorityScore(out.normalized);
	return out;
}

std::optional<string> resolveRelativeURL(const string& raw, const string& base_url) {

	// Reject empty
	if (raw.empty()) {
		return nullopt;
	}

	// Reject fragments-only
	if (raw[0] == '#') {
		return nullopt;
	}

	// Reject non-http(s) schemes early
	if (raw.starts_with("mailto:") ||
		raw.starts_with("javascript:") ||
		raw.starts_with("tel:")) {
		return nullopt;
	}

	// Parse base URL
	auto base = parse_uri(base_url);
	if (!base) {
		return nullopt;
	}

	// Parse reference
	auto ref = parse_uri_reference(raw);
	if (!ref) {
		return std::nullopt;
	}

	// Resolve (this handles absolute AND relative)
	url resolved = *base;
	resolved.resolve(*ref);

	// Only allow http / https
	if (resolved.scheme() != "http" &&
		resolved.scheme() != "https") {
		return std::nullopt;
	}

	// Strip fragment
	resolved.remove_fragment();

	return resolved.buffer();
}
