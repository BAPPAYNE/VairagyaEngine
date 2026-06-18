#include "pipeline/link_data_builder.hpp"

using namespace storage;

LinkData LinkDataBuilder::build(uint32_t outbound_count, uint32_t inbound_count, float pagerank) {
    LinkData link;
    link.outbound_links_count = outbound_count;
    link.inbound_links_count = inbound_count;
    link.pagerank_score = pagerank;
    return link;
}
