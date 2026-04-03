#ifndef DOC_CORE_BUILDER
#define DOC_CORE_BUILDER

#include "storage/db_schema.h"
#include <string>

class DocCoreBuilder {
public:
	static storage::DocCore build(const std::string& normalized_url, const std::string& html_header = ""); // fetch canonical_url, language_code, charset, content_type in form of DocCore.
	static std::string hashUrl(const std::string& normalized_url);
	static std::string getCanonicalUrl(const std::string& html_header);
	static std::string getLanguageCode(const std::string& html_header);
	static std::string getCharset(const std::string& html_header);
	static std::string getContentType(const std::string& html_header);
};


#endif // !DOC_CORE_BUILDER