cmake_minimum_required(VERSION 3.20)

project(VairagyaEngine LANGUAGES CXX)

find_package(Boost REQUIRED COMPONENTS url)
find_package(RocksDB CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json REQUIRED)

add_executable(VairagyaEngine
    VairagyaEngine/src/main.cpp
    VairagyaEngine/src/url/normalize.cpp
    VairagyaEngine/src/url/validate.cpp
    VairagyaEngine/src/url/process.cpp
    VairagyaEngine/src/url/parse.cpp
    VairagyaEngine/src/crawler/frontier.cpp
    VairagyaEngine/src/crawler/scheduler.cpp
    VairagyaEngine/src/crawler/engine.cpp
    VairagyaEngine/src/net/fetcher.cpp
    VairagyaEngine/src/net/resolver.cpp
    VairagyaEngine/src/net/response_classifier.cpp
    VairagyaEngine/src/html/html_parser.cpp
    VairagyaEngine/src/host/rate_limit.cpp
    VairagyaEngine/src/host/robots_manager.cpp
    VairagyaEngine/src/storage/rocksdb_store.cpp
    VairagyaEngine/src/storage/url_state.cpp
    VairagyaEngine/src/storage/host_state.cpp
    VairagyaEngine/src/storage/content_state.cpp
    VairagyaEngine/src/storage/memory_url_state_store.cpp
    VairagyaEngine/src/storage/memory_content_state_store.cpp
    VairagyaEngine/src/storage/memory_host_state_store.cpp
    VairagyaEngine/src/utils/utils.cpp
    VairagyaEngine/src/utils/log.cpp
    VairagyaEngine/src/utils/config.cpp
    VairagyaEngine/src/utils/hash.cpp
    VairagyaEngine/src/pipeline/doc_core_builder.cpp
)

set_target_properties(VairagyaEngine PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_include_directories(VairagyaEngine
    PRIVATE
        ${PROJECT_SOURCE_DIR}/VairagyaEngine/include
)

target_link_libraries(VairagyaEngine
    PRIVATE
        Boost::url
        Boost::boost
        OpenSSL::SSL
        OpenSSL::Crypto
        nlohmann_json::nlohmann_json
        RocksDB::rocksdb
)
