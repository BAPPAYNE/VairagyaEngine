#include "url/process.h"
#include "url/validate.h"
#include "url/normalize.h"

#include <boost/url.hpp>
#include <iostream>
#include <algorithm>
#include <sstream>
#include <vector>
#include <cctype>
#include <initializer_list>
#include <string>

using namespace std;
using namespace boost::urls;

static bool shouldFetchBody(ResourceType type) {
	return type == ResourceType::HTML ||
		type == ResourceType::TEXT_DOCUMENT;
}

ProcessedURL processURL(const string& input) {
	ProcessedURL out;
	out.original = input;

	auto normalized = normalizeURI(input);
	if (!normalized) {
		return out;
	}

	out.normalized = *normalized;

	out.status = analyzeURL(out.normalized);
	if (out.status != URLStatus::ACCEPTED_URL) {
		return out;
	}

	out.scheme = extractScheme(out.normalized);
	out.crawlability = assessCrawlability(out.scheme);
	if (out.crawlability == Crawlability::NON_CRAWLABLE) {
		return out;
	}

	out.resource_type = classifyResourceType(out.normalized);
	if (!shouldFetchBody(out.resource_type)) {
		return out;
	}

	out.priority = priorityScore(out.normalized);
	if (out.priority <= 0) {
		out.priority = 0;
	}

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
		return nullopt;
	}

	// Resolve (this handles absolute AND relative)
	url resolved = *base;
	resolved.resolve(*ref);

	// Only allow http / https
	if (resolved.scheme() != "http" &&
		resolved.scheme() != "https") {
		return nullopt;
	}

	// Strip fragment
	resolved.remove_fragment();

	return resolved.buffer();
}

string reverseHost(const string& url_str) {
	try {
		auto parsed = boost::urls::parse_uri(url_str);
		if (!parsed) return "";
		string host = string(parsed->host());
		
		vector<string> parts;
		string part;
		istringstream stream(host);
		while (getline(stream, part, '.')) {
			parts.push_back(part);
		}
		
		string reversed;
		for (int i = (int)parts.size() - 1; i >= 0; --i) {
			reversed += parts[i];
			if (i > 0) reversed += ".";
		}
		return reversed;
	} catch (...) {
		return "";
	}
}
