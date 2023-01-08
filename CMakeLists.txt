cmake_minimum_required(VERSION 3.23.0)

project(bridge)

set(CMAKE_CXX_STANDARD 23)
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
set(XREPO_BOOTSTRAP_XMAKE ON)
set(XREPO_BUILD_PARALLEL_JOBS 12)
set(XMAKE_RELEASE_LATEST v2.7.5)

include(${CMAKE_BINARY_DIR}/xrepo.cmake)
if(NOT EXISTS "$ENV{HOME}/.xmake/repositories/my_private_repo/xmake.lua")
    message(STATUS "add my_private_repo")
    if(EXISTS ${CMAKE_BINARY_DIR}/xmake/install/bin/xmake)
        execute_process(COMMAND ${CMAKE_BINARY_DIR}/xmake/install/bin/xmake repo -a -g my_private_repo https://github.com/fantasy-peak/xmake-repo.git)
    else()
        execute_process(COMMAND xmake repo -a -g my_private_repo https://github.com/fantasy-peak/xmake-repo.git)
    endif()
endif()

#----------------------------------------------------------------------------------
xrepo_package("asio")
xrepo_package("async_simple main")
xrepo_package("fmt")
xrepo_package("spdlog")
xrepo_package("yaml_cpp_struct v1.0.0" DEPS "magic_enum" "visit_struct" "yaml-cpp")
xrepo_package("libuuid" CONFIGS "system=false")
#-----------------------------------------------------------------------------------
include_directories(include)
add_executable(bridge src/main.cpp)
xrepo_target_packages(bridge NO_LINK_LIBRARIES PUBLIC yaml_cpp_struct asio fmt spdlog async_simple)
target_link_libraries(bridge PUBLIC uuid yaml-cpp async_simple fmt pthread dl)
