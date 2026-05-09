cmake_minimum_required(VERSION 3.20)

project(VairagyaEngine LANGUAGES CXX)

# Suppress CMake policy warning for FindBoost removal (CMP0167)
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

# Use the config package for Boost.URL (as recommended by vcpkg/Boost.URL)
find_package(boost_url CONFIG REQUIRED)
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
    VairagyaEngine/src/query/query_processor.cpp
    VairagyaEngine/src/query/index_searcher.cpp
    VairagyaEngine/src/query/ranker.cpp
    VairagyaEngine/src/query/snippet_generator.cpp
    VairagyaEngine/src/query/result_builder.cpp
    VairagyaEngine/src/query/query_engine.cpp
    VairagyaEngine/src/api/search_api.cpp
    VairagyaEngine/src/utils/utils.cpp
    VairagyaEngine/src/utils/log.cpp
    VairagyaEngine/src/utils/config.cpp
    VairagyaEngine/src/utils/hash.cpp
    VairagyaEngine/src/pipeline/doc_core_builder.cpp
    VairagyaEngine/src/pipeline/fetch_meta_builder.cpp
    VairagyaEngine/src/pipeline/content_meta_builder.cpp
    VairagyaEngine/src/pipeline/parsed_content_builder.cpp
    VairagyaEngine/src/pipeline/link_data_builder.cpp
    VairagyaEngine/src/pipeline/quality_signals_builder.cpp
    VairagyaEngine/src/pipeline/presentation_builder.cpp
    VairagyaEngine/src/pipeline/control_flags_builder.cpp
    VairagyaEngine/external/simHash-cpp/simhash.cpp
    VairagyaEngine/external/simHash-cpp/permutation.cpp
)

set_target_properties(VairagyaEngine PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_include_directories(VairagyaEngine
    PRIVATE
        ${PROJECT_SOURCE_DIR}/VairagyaEngine/include
    PUBLIC
        ${PROJECT_SOURCE_DIR}/VairagyaEngine/external/simHash-cpp
)

target_link_libraries(VairagyaEngine
    PRIVATE
        Boost::url
        OpenSSL::SSL
        OpenSSL::Crypto
        nlohmann_json::nlohmann_json
        RocksDB::rocksdb
)

if(WIN32)
    target_compile_definitions(VairagyaEngine PRIVATE _WIN32_WINNT=0x0601)
    target_link_libraries(VairagyaEngine PRIVATE ws2_32 mswsock)
endif()
