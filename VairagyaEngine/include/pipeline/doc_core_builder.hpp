#ifndef DOC_CORE_BUILDER
#define DOC_CORE_BUILDER

#include "storage/db_schema.hpp"
#include <string>

using namespace std;

class DocCoreBuilder {
public:
	static storage::DocCore build(const string& normalized_url, const string& html_header = ""); // fetch canonical_url, language_code, charset, content_type in form of DocCore.
	static string hashUrl(const string& normalized_url);
	static string getCanonicalUrl(const string& html_header);
	static string getLanguageCode(const string& html_header);
	static string getCharset(const string& html_header);
	static string getContentType(const string& html_header);
};


#endif // !DOC_CORE_BUILDER