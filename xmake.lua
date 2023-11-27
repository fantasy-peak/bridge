set_project("bridge")
set_version("0.0.1", {build = "%Y%m%d%H%M"})
set_xmakever("2.7.8")

add_repositories("my_private_repo https://github.com/fantasy-peak/xmake-repo.git")

add_requires("asio")
add_requires("fmt", "nlohmann_json", "spdlog", "gflags")

set_languages("c++23")
set_policy("check.auto_ignore_flags", false)
add_cxflags("-O2 -Wall -Wextra -pedantic-errors -Wno-missing-field-initializers -Wno-ignored-qualifiers")

target("bridge")
    set_kind("binary")
    add_files("src/*.cpp")
    add_packages( "spdlog", "fmt", "asio", "gflags")
    add_syslinks("pthread")
target_end()
