-- Dynamic Performance Tuner for Fallout 4
-- An F4SE plugin built on CommonLibF4 (multi-runtime: OG / NG / AE).

includes("lib/commonlibf4")

local plugin_name = "PerformanceTuner"
local plugin_version = "1.0.0"
local plugin_version_major, plugin_version_minor, plugin_version_patch =
    plugin_version:match("^(%d+)%.(%d+)%.(%d+)$")

set_project(plugin_name)
set_version(plugin_version)
set_license("MIT")
set_languages("c++23")
set_warnings("allextra")

add_rules("mode.release", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

-- Resolve REL::ID and structure layouts for all three runtime families from one binary.
add_defines("COMMONLIB_RUNTIMECOUNT=3")

target(plugin_name)
    add_rules("commonlibf4.plugin", {
        name = plugin_name,
        author = "Seel",
        description = "Dynamic shadow distance and godray quality tuner for Fallout 4",
    })

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

    add_defines(
        'PLUGIN_NAME="' .. plugin_name .. '"',
        "PLUGIN_VERSION_MAJOR=" .. plugin_version_major,
        "PLUGIN_VERSION_MINOR=" .. plugin_version_minor,
        "PLUGIN_VERSION_PATCH=" .. plugin_version_patch
    )

    add_installfiles("res/PerformanceTuner.ini", { prefixdir = "F4SE/Plugins" })
