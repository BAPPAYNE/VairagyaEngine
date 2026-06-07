#include "pipeline/doc_core_builder.h"
#include "utils/hash.h"

#include <algorithm>
#include <cctype>

using namespace std;

namespace {
	constexpr size_t MAX_HEAD_BYTES = 128u * 1024;

	string toLowerAscii(string value) {
		transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
			return static_cast<char>(tolower(c));
		});
		return value;
	}

	void trimInPlace(string& value) {
		const auto begin = find_if_not(value.begin(), value.end(), [](unsigned char c) {
			return isspace(c);
		});
		const auto end = find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
			return isspace(c);
		}).base();

		if (begin >= end) {
			value.clear();
			return;
		}
		value.assign(begin, end);
	}

	bool startsWithIgnoreCase(const string& text, size_t pos, const string& needle) {
		if (pos + needle.size() > text.size()) {
			return false;
		}
		for (size_t i = 0; i < needle.size(); ++i) {
			if (tolower(static_cast<unsigned char>(text[pos + i])) !=
				tolower(static_cast<unsigned char>(needle[i]))) {
				return false;
			}
		}
		return true;
	}

	size_t findIgnoreCase(const string& text, const string& needle, size_t pos = 0) {
		if (needle.empty() || needle.size() > text.size()) {
			return string::npos;
		}
		for (size_t i = pos; i + needle.size() <= text.size(); ++i) {
			if (startsWithIgnoreCase(text, i, needle)) {
				return i;
			}
		}
		return string::npos;
	}

	string getAttributeValue(const string& tag, const string& attr_name) {
		const string lower = toLowerAscii(tag);
		const string attr = toLowerAscii(attr_name);
		size_t pos = 0;

		while ((pos = lower.find(attr, pos)) != string::npos) {
			const bool left_ok = pos == 0 ||
				!(isalnum(static_cast<unsigned char>(lower[pos - 1])) || lower[pos - 1] == '-' || lower[pos - 1] == '_');
			const size_t after = pos + attr.size();
			const bool right_ok = after >= lower.size() ||
				!(isalnum(static_cast<unsigned char>(lower[after])) || lower[after] == '-' || lower[after] == '_');

			if (!left_ok || !right_ok) {
				pos = after;
				continue;
			}

			size_t eq = lower.find('=', after);
			if (eq == string::npos) {
				return "";
			}
			for (size_t i = after; i < eq; ++i) {
				if (!isspace(static_cast<unsigned char>(lower[i]))) {
					pos = after;
					eq = string::npos;
					break;
				}
			}
			if (eq == string::npos) {
				continue;
			}

			size_t value_start = eq + 1;
			while (value_start < tag.size() && isspace(static_cast<unsigned char>(tag[value_start]))) {
				++value_start;
			}
			if (value_start >= tag.size()) {
				return "";
			}

			char quote = 0;
			if (tag[value_start] == '"' || tag[value_start] == '\'') {
				quote = tag[value_start++];
			}

			size_t value_end = value_start;
			if (quote) {
				value_end = tag.find(quote, value_start);
				if (value_end == string::npos) {
					value_end = tag.size();
				}
			}
			else {
				while (value_end < tag.size() &&
					!isspace(static_cast<unsigned char>(tag[value_end])) &&
					tag[value_end] != '>') {
					++value_end;
				}
			}

			string value = tag.substr(value_start, value_end - value_start);
			trimInPlace(value);
			return value;
		}

		return "";
	}

	string tagName(const string& tag) {
		size_t pos = tag.find('<');
		if (pos == string::npos) {
			return "";
		}
		++pos;
		if (pos < tag.size() && tag[pos] == '/') {
			++pos;
		}
		while (pos < tag.size() && isspace(static_cast<unsigned char>(tag[pos]))) {
			++pos;
		}

		size_t end = pos;
		while (end < tag.size() &&
			(isalnum(static_cast<unsigned char>(tag[end])) || tag[end] == '-' || tag[end] == ':')) {
			++end;
		}

		return toLowerAscii(tag.substr(pos, end - pos));
	}

	string firstTokenBeforeSemicolon(string value) {
		const size_t semicolon = value.find(';');
		if (semicolon != string::npos) {
			value = value.substr(0, semicolon);
		}
		trimInPlace(value);
		return value;
	}
}

// Call this to fetch canonical_url, language_code, charset, content_type in form of DocCore.
storage::DocCore DocCoreBuilder::build(const string& normalized_url, const string& html_header) {
	storage::DocCore doc_core;
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
	const string src = html_header.size() > MAX_HEAD_BYTES ? html_header.substr(0, MAX_HEAD_BYTES) : html_header;
	size_t pos = 0;

	while ((pos = findIgnoreCase(src, "<link", pos)) != string::npos) {
		const size_t end = src.find('>', pos);
		if (end == string::npos) {
			break;
		}
		const string tag = src.substr(pos, end - pos + 1);
		const string rel = toLowerAscii(getAttributeValue(tag, "rel"));
		if (rel.find("canonical") != string::npos) {
			return getAttributeValue(tag, "href");
		}
		pos = end + 1;
	}

	return string();
}

string DocCoreBuilder::getLanguageCode(const string& html_header) {
	if (html_header.empty()) return "en";
	const string src = html_header.size() > MAX_HEAD_BYTES ? html_header.substr(0, MAX_HEAD_BYTES) : html_header;
	const size_t pos = findIgnoreCase(src, "<html");
	if (pos != string::npos) {
		const size_t end = src.find('>', pos);
		if (end != string::npos) {
			const string lang = getAttributeValue(src.substr(pos, end - pos + 1), "lang");
			if (!lang.empty()) {
				return lang;
			}
		}
	}
	return "en"; // Default to English
}

string DocCoreBuilder::getCharset(const string& html_header) {
	if (html_header.empty()) return "UTF-8";
	const string src = html_header.size() > MAX_HEAD_BYTES ? html_header.substr(0, MAX_HEAD_BYTES) : html_header;
	size_t pos = 0;

	while ((pos = findIgnoreCase(src, "<meta", pos)) != string::npos) {
		const size_t end = src.find('>', pos);
		if (end == string::npos) {
			break;
		}
		const string tag = src.substr(pos, end - pos + 1);
		const string direct_charset = getAttributeValue(tag, "charset");
		if (!direct_charset.empty()) {
			return direct_charset;
		}

		const string content = getAttributeValue(tag, "content");
		const string lower_content = toLowerAscii(content);
		const size_t charset_pos = lower_content.find("charset=");
		if (charset_pos != string::npos) {
			size_t value_start = charset_pos + 8;
			while (value_start < content.size() && isspace(static_cast<unsigned char>(content[value_start]))) {
				++value_start;
			}
			size_t value_end = value_start;
			while (value_end < content.size() &&
				!isspace(static_cast<unsigned char>(content[value_end])) &&
				content[value_end] != ';') {
				++value_end;
			}
			string charset = content.substr(value_start, value_end - value_start);
			trimInPlace(charset);
			if (!charset.empty()) {
				return charset;
			}
		}
		pos = end + 1;
	}

	return "UTF-8"; // Default
}

string DocCoreBuilder::getContentType(const string& html_header) {
	if (html_header.empty()) return "text/html";
	const string src = html_header.size() > MAX_HEAD_BYTES ? html_header.substr(0, MAX_HEAD_BYTES) : html_header;
	size_t pos = 0;

	while ((pos = findIgnoreCase(src, "<meta", pos)) != string::npos) {
		const size_t end = src.find('>', pos);
		if (end == string::npos) {
			break;
		}
		const string tag = src.substr(pos, end - pos + 1);
		if (tagName(tag) == "meta" && toLowerAscii(getAttributeValue(tag, "http-equiv")) == "content-type") {
			const string content = firstTokenBeforeSemicolon(getAttributeValue(tag, "content"));
			if (!content.empty()) {
				return content;
			}
		}
		pos = end + 1;
	}

	return "text/html"; // Default
}
