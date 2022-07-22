
package("async_simple")

set_homepage("https://github.com/nqf/async_simple.git")
set_description("")

add_urls("https://github.com/nqf/async_simple.git")

on_install("linux", function (package)
    local configs = {}
    table.insert(configs, "-DCMAKE_BUILD_TYPE=Release")
    table.insert(configs, "-DASYNC_SIMPLE_ENABLE_TESTS=OFF")
    import("package.tools.cmake").install(package, configs)
    os.rm(package:installdir("lib/libasync_simple.so"))
end)

package_end() 