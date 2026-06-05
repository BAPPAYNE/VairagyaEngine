#ifndef SEARCH_API_H
#define SEARCH_API_H

#include "storage/rocksdb_store.h"

#include <cstdint>
#include <memory>

using namespace std;

namespace api {

    void runSearchApi(shared_ptr<storage::RocksDBStore> &db_store, uint16_t port);
    
}

#endif
