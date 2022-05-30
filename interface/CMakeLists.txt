cmake_minimum_required(VERSION 3.13.0)

project(bridge_interface)

set(CMAKE_CXX_STANDARD 20)

SET(CMAKE_CXX_FLAGS "-ggdb -O3 -fPIC -std=c++20")

add_library(bridge_interface SHARED interface.cpp)
xrepo_target_packages(bridge_interface PUBLIC fmt spdlog nlohmann_json drogon redis-plus-plus NO_LINK_LIBRARIES)
target_link_libraries(bridge_interface
    drogon trantor jsoncpp brotlidec brotlienc brotlicommon z
    redis++ hiredis fmt uv
    boost_context-mt boost_program_options-mt boost_system-mt boost_thread-mt boost_regex-mt boost_filesystem-mt boost_atomic-mt
    ssl crypto cares uuid pthread dl
)
