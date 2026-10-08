# DungeonGeometryGeneratorCli: F10 machine loop — every pipeline step (F1..F11) as a CLI command
# with PGG-style diagnostics. Links the whole dungeon_geometry_generator pipeline plus the D4 export.
nw_add_console_app(
    GROUP apps/tools
    LIBS
        dungeon_geometry_generator
        dungeon_geometry_generator_layout
        dungeon_geometry_generator_fill
        dungeon_geometry_generator_check
        dungeon_geometry_generator_export
)

# F10 smoke: the machine loop end to end. Working directory is the repo root so
# the assets fallback resolves like in the DungeonGeometryGeneratorViewer smokes. F11 checks run
# on the small layout fixture — on the frozen level they take minutes in Debug.
add_test(NAME DungeonGeometryGeneratorCli_smoke_validate
         COMMAND DungeonGeometryGeneratorCli validate "${CMAKE_SOURCE_DIR}/src/apps/DungeonGeometryGeneratorViewer/smoke_project.json")
add_test(NAME DungeonGeometryGeneratorCli_smoke_layout
         COMMAND DungeonGeometryGeneratorCli layout "${CMAKE_SOURCE_DIR}/src/apps/DungeonGeometryGeneratorViewer/smoke_project.json"
                 -o "${CMAKE_CURRENT_BINARY_DIR}/smoke_layout.json")
set_tests_properties(DungeonGeometryGeneratorCli_smoke_layout PROPERTIES PASS_REGULAR_EXPRESSION "smoke_layout.json")
add_test(NAME DungeonGeometryGeneratorCli_smoke_export
         COMMAND DungeonGeometryGeneratorCli export "${CMAKE_SOURCE_DIR}/src/tests/data/d1_project.json"
                 --ir "${CMAKE_SOURCE_DIR}/docs/dungeon_geometry_generator/d0/frozen_ir.json"
                 -o "${CMAKE_CURRENT_BINARY_DIR}/smoke_out")
set_tests_properties(DungeonGeometryGeneratorCli_smoke_export PROPERTIES PASS_REGULAR_EXPRESSION "d1_project.obj")
add_test(NAME DungeonGeometryGeneratorCli_smoke_check
         COMMAND DungeonGeometryGeneratorCli check "${CMAKE_SOURCE_DIR}/src/apps/DungeonGeometryGeneratorViewer/smoke_project.json")
set_tests_properties(DungeonGeometryGeneratorCli_smoke_check PROPERTIES PASS_REGULAR_EXPRESSION "check: ok")
# The error path: an unknown key must exit 1 with a D101 diagnostic (a driver
# script, since WILL_FAIL does not compose with PASS_REGULAR_EXPRESSION).
add_test(NAME DungeonGeometryGeneratorCli_smoke_errors
         COMMAND ${CMAKE_COMMAND}
                 -DCLI_EXE=$<TARGET_FILE:${AppName}>
                 -DBAD_PROJECT=${CMAKE_SOURCE_DIR}/src/tests/data/d4_bad_project.json
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/smoke_errors.cmake")
set_tests_properties(DungeonGeometryGeneratorCli_smoke_validate DungeonGeometryGeneratorCli_smoke_layout DungeonGeometryGeneratorCli_smoke_export
                     DungeonGeometryGeneratorCli_smoke_check DungeonGeometryGeneratorCli_smoke_errors
                     PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
