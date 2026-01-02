cmake_minimum_required(VERSION 3.20)

project(VairagyaEngine LANGUAGES CXX)

find_package(Boost)



add_executable(VairagyaEngine
    ${PROJECT_SOURCE_DIR}/VairagyaEngine/src/main.cpp
    "VairagyaEngine/src/url/normalize.cpp"
    "VairagyaEngine/src/url/validate.cpp"
    "VairagyaEngine/src/url/process.cpp"
    "VairagyaEngine/src/url/parse.cpp"
    "VairagyaEngine/src/crawler/frontier.cpp"
    "VairagyaEngine/src/crawler/scheduler.cpp" 
    "VairagyaEngine/src/crawler/engine.cpp"
    "VairagyaEngine/src/net/fetcher.cpp"
    "VairagyaEngine/src/net/resolver.cpp"
    "VairagyaEngine/src/host/rate_limit.cpp"
    "VairagyaEngine/src/host/robots.cpp"
)
target_link_libraries(VairagyaEngine PRIVATE Boost::boost)
set_target_properties(VairagyaEngine PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_include_directories(VairagyaEngine
    PRIVATE
        ${PROJECT_SOURCE_DIR}/VairagyaEngine/include
        $ENV{VCPKG_INCLUDE}
)

find_package(Boost REQUIRED COMPONENTS url)

target_link_libraries(VairagyaEngine
    PRIVATE
        Boost::url
)

if(DEFINED ENV{VCPKG_INCLUDE})
    target_include_directories(VairagyaEngine PRIVATE $ENV{VCPKG_INCLUDE})
else()
    message(FATAL_ERROR "VCPKG_INCLUDE is not set")
endif()