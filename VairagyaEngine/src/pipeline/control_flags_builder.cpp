#include "pipeline/control_flags_builder.h"
#include <regex>

using namespace std;
using namespace storage;

ControlFlags ControlFlagsBuilder::build(const string& html, bool robots_allowed) {
    ControlFlags cf;
    cf.robots_allowed = robots_allowed;
    cf.noindex = checkNoIndex(html);
    cf.nofollow = checkNoFollow(html);
    cf.index_status = (cf.noindex ? "SKIPPED_NOINDEX" : "PENDING");
    return cf;
}

bool ControlFlagsBuilder::checkNoIndex(const string& html) {
    static const regex meta_robots(R"(<meta\b[^>]*\bname\s*=\s*['"]robots['"][^>]*\bcontent\s*=\s*['"][^'"]*noindex[^'"]*['"])", regex::icase);
    return regex_search(html, meta_robots);
}

bool ControlFlagsBuilder::checkNoFollow(const string& html) {
    static const regex meta_robots(R"(<meta\b[^>]*\bname\s*=\s*['"]robots['"][^>]*\bcontent\s*=\s*['"][^'"]*nofollow[^'"]*['"])", regex::icase);
    return regex_search(html, meta_robots);
}
