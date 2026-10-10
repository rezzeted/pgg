# DungeonGeometryGeneratorViewer: F9 preview — 3D + top view of a filled dungeon_geometry_generator level with an IR
# overlay. Links the dungeon_geometry_generator pipeline (project/layout/fill) and the shared
# offscreen pgg preview.
find_package(imgui CONFIG REQUIRED)

nw_add_console_app(
    GROUP apps/tools
    LIBS
        pgg
        pgg_preview
        dungeon_geometry_generator
        dungeon_geometry_generator_layout
        dungeon_geometry_generator_fill
        spdlog::spdlog_header_only
        imgui::imgui
)

find_path(SOKOL_INCLUDE_DIRS "sokol_app.h")
if(NOT SOKOL_INCLUDE_DIRS)
    message(FATAL_ERROR "Sokol headers not found (sokol_app.h). Make sure vcpkg dependency 'sokol' is installed.")
endif()
target_include_directories(${AppName} PRIVATE ${SOKOL_INCLUDE_DIRS})

nw_configure_sokol_app(${AppName})

# Symbol names in the crash handler's backtraces (run_log.cpp, -rdynamic).
if(UNIX)
    set_target_properties(${AppName} PROPERTIES ENABLE_EXPORTS ON)
endif()

# F9 smoke: the data path (layout -> IR -> fill) without a window. Projects
# resolve their slots through the FillOpts::dungeon_geometry_generator_assets fallback, so the repo
# assets dir is found from the executable upward; the working directory only
# shortens the search.
# The smoke uses the app's own fixture: src/tests/data/d2_project.json can
# never build an IR (5.2: its corridor role overrides wall_t, so corridor/
# room shared walls resolve different thicknesses — an F4 error by design).
add_test(NAME DungeonGeometryGeneratorViewer_smoke_layout
         COMMAND DungeonGeometryGeneratorViewer --smoke "${CMAKE_CURRENT_SOURCE_DIR}/smoke_project.json")
set_tests_properties(DungeonGeometryGeneratorViewer_smoke_layout
                     PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
