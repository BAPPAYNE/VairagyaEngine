#include "pipeline/content_meta_builder.hpp"
#include "utils/hash.hpp"
#include "simhash.h"
#include <sstream>
#include <vector>

using namespace storage;
using namespace std;

ContentMeta ContentMetaBuilder::build(const string& clean_text, uint64_t canonical_doc_id) {
    ContentMeta meta;
    meta.content_hash = sha256(clean_text);
    meta.simhash = computeSimhash(clean_text);
    meta.is_duplicate = (canonical_doc_id > 0);
    meta.canonical_doc_id = canonical_doc_id;
    return meta;
}

uint64_t ContentMetaBuilder::computeSimhash(const string& text) {
    if (text.empty()) return 0;

    vector<Simhash::hash_t> feature_hashes;
    stringstream ss(text);
    string token;

    // Tokenize by whitespace and compute features
    while (ss >> token) {
        // FNV-1a 64-bit hash for the token
        uint64_t h = 14695981039346656037ULL; // FNV-1a 64-bit offset basis
        for (char c : token) {
            h ^= static_cast<unsigned char>(c);
            h *= 1099511628211ULL; // FNV-1a 64-bit prime
        }
        feature_hashes.push_back(h);
    }

    if (feature_hashes.empty()) return 0;

    // Use the external Simhash library to compute the overall document simhash
    return Simhash::compute(feature_hashes);
}
