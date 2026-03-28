#ifndef DOC_CORE
#define DOC_CORE

#include <cstdint>
#include <string>
#include <ctime>

using namespace std;

struct DocCore {
	uint64_t doc_id; // internal primary key
	string normalized_url; // normalized URL
	string url_hash; // hash of the URL, used for quick lookup
	string canonical_url; // normalized URL, used for deduplication
	time_t first_seen_time; // timestamp of when the URL was discovered
	string language_code; // ISO 639-1 language code, e.g., "en" for English
	string charset; // character encoding of the document, e.g., "UTF-8, ASCII, ANSI"
	string content_type; // MIME type of the document, e.g., "text/html"
};

#endif // !DOC_CORE
