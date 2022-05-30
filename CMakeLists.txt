cmake_minimum_required(VERSION 3.13.0)

project(bridge)

set(CMAKE_CXX_STANDARD 20)

set(OPTFLAGS -gdwarf-2)
set(WARNINGS -Wall -Wno-missing-field-initializers -Wno-ignored-qualifiers)
add_compile_options(${OPTFLAGS} ${WARNINGS})

if(NOT EXISTS "${CMAKE_BINARY_DIR}/xrepo.cmake")
    message(STATUS "Downloading xrepo.cmake from https://github.com/xmake-io/xrepo-cmake/")
    # mirror https://cdn.jsdelivr.net/gh/xmake-io/xrepo-cmake@main/xrepo.cmake
    file(DOWNLOAD "https://raw.githubusercontent.com/xmake-io/xrepo-cmake/main/xrepo.cmake"
                  "${CMAKE_BINARY_DIR}/xrepo.cmake"
                  TLS_VERIFY ON)
endif()

set(XREPO_PACKAGE_VERBOSE ON)
set(XREPO_BOOTSTRAP_XMAKE OFF)
set(XREPO_BUILD_PARALLEL_JOBS 12)

include(${CMAKE_BINARY_DIR}/xrepo.cmake)

execute_process(COMMAND xmake repo -a -g my_private_repo https://github.com/fantasy-peak/xmake-repo.git)

#----------------------------------------------------------------------------------

xrepo_package("fmt 8.1.1")
xrepo_package("gflags v2.2.2")
xrepo_package("spdlog v1.9.2")
xrepo_package("nlohmann_json v3.10.5")

set(XREPO_XMAKEFILE ${CMAKE_CURRENT_SOURCE_DIR}/repo/packages/d/drogon/xmake.lua)
xrepo_package("drogon" DEPS "trantor" "jsoncpp 1.9.5" "zlib v1.2.12" "brotli 1.0.9")

set(XREPO_XMAKEFILE ${CMAKE_CURRENT_SOURCE_DIR}/repo/packages/r/redis-plus-plus/xmake.lua)
xrepo_package("redis-plus-plus" DEPS "hiredis" "boost 1.79.0")

set(XREPO_XMAKEFILE ${CMAKE_CURRENT_SOURCE_DIR}/repo/packages/b/boost/xmake.lua)
xrepo_package("boost 1.79.0")

set(XREPO_XMAKEFILE ${CMAKE_CURRENT_SOURCE_DIR}/repo/packages/y/yaml_cpp_struct/xmake.lua)
xrepo_package("yaml_cpp_struct main" DEPS "magic_enum" "visit_struct" "yaml-cpp")

#----------------------------------------------------------------------------------

include_directories(include)

add_executable(bridge
    src/main.cpp
)
xrepo_target_packages(bridge PUBLIC gflags fmt yaml_cpp_struct spdlog nlohmann_json drogon redis-plus-plus NO_LINK_LIBRARIES)
target_link_libraries(bridge
    gflags drogon trantor jsoncpp brotlidec brotlienc brotlicommon z
    redis++ hiredis fmt yaml-cpp uv
    boost_context-mt boost_program_options-mt boost_system-mt boost_thread-mt boost_regex-mt boost_filesystem-mt boost_atomic-mt
    ssl crypto cares uuid pthread dl
)

add_subdirectory(interface)
