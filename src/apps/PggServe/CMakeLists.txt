# PggServe: headless GPU daemon for agent RPC (slots by .pgg path).
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

nw_add_console_app(
    GROUP apps/tools
    LIBS
        pgg
        pgg_preview
        spdlog::spdlog_header_only
        nlohmann_json::nlohmann_json
)

find_path(SOKOL_INCLUDE_DIRS "sokol_app.h")
if(NOT SOKOL_INCLUDE_DIRS)
    message(FATAL_ERROR "Sokol headers not found (sokol_app.h). Make sure vcpkg dependency 'sokol' is installed.")
endif()
target_include_directories(${AppName} PRIVATE ${SOKOL_INCLUDE_DIRS})

nw_configure_sokol_app(${AppName})
