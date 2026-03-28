#include "pipeline/doc_core_builder.h"
#include "utils/hash.h"
#include <regex>

using namespace std;

// file-local regex (case-insensitive):
// - lookahead ensures rel contains the token "canonical" anywhere in the tag
// - capture group 1 is the href value (works regardless of attribute order)
static const regex canonical_regex(
	R"(<link\b(?=[^>]*\brel\s*=\s*['"][^'"]*\bcanonical\b[^'"]*['"])[^>]*\bhref\s*=\s*['"]([^'"]+)['"][^>]*>)",
	std::regex::icase
);

// Call this to fetch canonical_url, language_code, charset, content_type in form of DocCore.
DocCore DocCoreBuilder::build(const string& normalized_url, const string& html_header) {
	DocCore doc_core;
	doc_core.normalized_url = normalized_url;
	doc_core.url_hash = hashUrl(normalized_url);
	// Only attempt canonical extraction if HTML was provided
	if (!html_header.empty()) {
		doc_core.canonical_url = getCanonicalUrl(html_header);
	}
	else {
		doc_core.canonical_url = string();
	}
	doc_core.first_seen_time = time(nullptr);
	doc_core.language_code = getLanguageCode(html_header);
	doc_core.charset = getCharset(html_header);
	doc_core.content_type = getContentType(html_header);
	return doc_core;
}

string DocCoreBuilder::hashUrl(const string& normalized_url) {
	return sha256(normalized_url);
}

string DocCoreBuilder::getCanonicalUrl(const string& html_header) {
	// Use sregex_iterator which is the convenience typedef for string iterators.
	sregex_iterator it(html_header.begin(), html_header.end(), canonical_regex);
	sregex_iterator end;
	for (; it != end; ++it) {
		const smatch& m = *it;
		// m[0] = full match, m[1] = href capture
		if (m.size() >= 2) {
			return m[1].str();
		}
	}
	return string();
}

string DocCoreBuilder::getLanguageCode(const string& html_header) {
	if (html_header.empty()) return "en";
	string search_area = html_header.substr(0, 8192); // Search only head portion
	static const regex lang_regex(
		R"(<html\b[^>]*\blang\s*=\s*['"]([^'"]+)['"])",
		std::regex::icase
	);
	smatch m;
	if (regex_search(search_area, m, lang_regex) && m.size() >= 2) {
		return m[1].str();
	}
	return "en"; // Default to English
}

string DocCoreBuilder::getCharset(const string& html_header) {
	if (html_header.empty()) return "UTF-8";
	string search_area = html_header.substr(0, 8192); // Search only head portion
	
	// 1. Try HTML5 <meta charset="utf-8">
	static const regex charset_regex1(
		R"(<meta\b[^>]*\bcharset\s*=\s*['"]?([^'"\s>]+)['"]?)",
		std::regex::icase
	);
	smatch m;
	if (regex_search(search_area, m, charset_regex1) && m.size() >= 2) {
		return m[1].str();
	}

	// 2. Try HTML4 <meta http-equiv="content-type" content="text/html; charset=utf-8">
	static const regex charset_regex2(
		R"(<meta\b[^>]*\bcontent\s*=\s*['"][^'"]*charset\s*=\s*([^'"\s;>]+)[^'"]*['"][^>]*>)",
		std::regex::icase
	);
	if (regex_search(search_area, m, charset_regex2) && m.size() >= 2) {
		return m[1].str();
	}

	return "UTF-8"; // Default
}

string DocCoreBuilder::getContentType(const string& html_header) {
	if (html_header.empty()) return "text/html";
	string search_area = html_header.substr(0, 8192); // Search only head portion
	
	// Look for <meta http-equiv="content-type" content="text/html; charset=utf-8">
	static const regex content_type_regex(
		R"(<meta\b(?=[^>]*\bhttp-equiv\s*=\s*['"]content-type['"])[^>]*\bcontent\s*=\s*['"]([^'"\s;>]+)[^'"]*['"])",
		std::regex::icase
	);
	smatch m;
	if (regex_search(search_area, m, content_type_regex) && m.size() >= 2) {
		return m[1].str();
	}

	return "text/html"; // Default
}