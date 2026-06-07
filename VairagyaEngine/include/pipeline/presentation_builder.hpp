#ifndef PRESENTATION_BUILDER_H
#define PRESENTATION_BUILDER_H

#include <storage/db_schema.h>
#include <string>

using namespace std;

class PresentationBuilder {
public:
    static storage::Presentation build(
        const string& clean_text, // clean text of the document
        const string& html, // html content of the document
        const string& site_name, // site name of the document
        const string& display_url, // display url of the document
        const string& breadcrumb // breadcrumb of the document
    );
    static string generateSnippet(const string& text, size_t max_len = 250); // generate snippet from text
    static string extractFavicon(const string& html); // extract favicon from html content
};

#endif // PRESENTATION_BUILDER_H
