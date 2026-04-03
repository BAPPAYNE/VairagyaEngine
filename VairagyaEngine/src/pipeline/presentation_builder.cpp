#include "pipeline/presentation_builder.h"
#include <regex>

using namespace std;
using namespace storage;

Presentation PresentationBuilder::build(
    const string& clean_text, 
    const string& html, 
    const string& site_name, 
    const string& display_url,
    const string& breadcrumb
) {
    Presentation p;
    p.snippet = generateSnippet(clean_text);
    p.favicon_url = extractFavicon(html);
    p.site_name = site_name;
    p.display_url = display_url;
    p.breadcrumb = breadcrumb;
    return p;
}

string PresentationBuilder::generateSnippet(const string& text, size_t max_len) {
    if (text.length() <= max_len) return text;
    return text.substr(0, max_len) + "...";
}

string PresentationBuilder::extractFavicon(const string& html) {
    static const regex icon_regex(R"(<link\b[^>]*\brel\s*=\s*['"](?:shortcut\s+)?icon['"][^>]*\bhref\s*=\s*['"]([^'"]+)['"])", regex::icase);
    smatch m;
    if (regex_search(html, m, icon_regex) && m.size() >= 2) {
        return m[1].str();
    }
    return "/favicon.ico";
}
