#ifndef PRESENTATION_BUILDER_H
#define PRESENTATION_BUILDER_H

#include <storage/db_schema.h>
#include <string>

class PresentationBuilder {
public:
    static storage::Presentation build(
        const std::string& clean_text, // clean text of the document
        const std::string& html, // html content of the document
        const std::string& site_name, // site name of the document
        const std::string& display_url, // display url of the document
        const std::string& breadcrumb // breadcrumb of the document
    );
    static std::string generateSnippet(const std::string& text, size_t max_len = 250); // generate snippet from text
    static std::string extractFavicon(const std::string& html); // extract favicon from html content
};

#endif // PRESENTATION_BUILDER_H
