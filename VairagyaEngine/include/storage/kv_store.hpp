#ifndef KV_STORE_H
#define KV_STORE_H

#include <string>
#include <vector>
#include <optional>

using namespace std;

namespace storage {

    class KVStore {
    public:
        virtual ~KVStore() = default;

        virtual bool open(const string& path) = 0;
        virtual void close() = 0;

        // Generic Put/Get for Column Families
        virtual bool put(const string& cf_name, const string& key, const string& value) = 0;
        virtual optional<string> get(const string& cf_name, const string& key) = 0;
        virtual bool del(const string& cf_name, const string& key) = 0;
    };

} // namespace storage

#endif // KV_STORE_H
