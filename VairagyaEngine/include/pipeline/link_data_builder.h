#ifndef LINK_DATA_BUILDER_H
#define LINK_DATA_BUILDER_H

#include <storage/db_schema.h>
#include <vector>
#include <string>

class LinkDataBuilder {
public:
    static storage::LinkData build(uint32_t outbound_count, uint32_t inbound_count = 0, float pagerank = 0.0f);
};

#endif // LINK_DATA_BUILDER_H
