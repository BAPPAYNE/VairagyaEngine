#ifndef DOC_CORE_BUILDER
#define DOC_CORE_BUILDER

#include <pipeline/doc_core.h>
#include <string>

class DocCoreBuilder {
public:
	static DocCore build(const std::string& normalized_url, const std::string& html_header = ""); // fetch canonical_url, language_code, charset, content_type in form of DocCore.
	static string hashUrl(const string& normalized_url); // hashing is separate from build to allow for potential reuse in other contexts (e.g., URL deduplication)
	static string getCanonicalUrl(const string& html_header); // parses the HTML header to find the canonical URL, returns empty string if not found
	static string getLanguageCode(const string& html_header); // Analyzes the document content or headers to determine the language code.
	static string getCharset(const string& html_header); // Analyzes the document content or headers to determine the charset.
	static string getContentType(const string& html_header); // Analyzes the document content or headers to determine the content type.
};


#endif // !DOC_CORE_BUILDER