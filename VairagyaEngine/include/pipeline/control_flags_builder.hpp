#ifndef CONTROL_FLAGS_BUILDER_H
#define CONTROL_FLAGS_BUILDER_H

#include <storage/db_schema.hpp>
#include <string>

using namespace std;

class ControlFlagsBuilder {
public:
    static storage::ControlFlags build(const string& html, bool robots_allowed); // build control flags from html content
    static bool checkNoIndex(const string& html); // check noindex from html content
    static bool checkNoFollow(const string& html); // check nofollow from html content
};

#endif // CONTROL_FLAGS_BUILDER_H
