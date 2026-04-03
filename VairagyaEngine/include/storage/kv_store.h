#ifndef KV_STORE_H
#define KV_STORE_H

#include <string>
#include <vector>
#include <optional>

namespace storage {

    class KVStore {
    public:
        virtual ~KVStore() = default;

        virtual bool open(const std::string& path) = 0;
        virtual void close() = 0;

        // Generic Put/Get for Column Families
        virtual bool put(const std::string& cf_name, const std::string& key, const std::string& value) = 0;
        virtual std::optional<std::string> get(const std::string& cf_name, const std::string& key) = 0;
        virtual bool del(const std::string& cf_name, const std::string& key) = 0;
    };

} // namespace storage

#endif // KV_STORE_H
