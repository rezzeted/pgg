# BuildingGeneratorCli (B0): Building IR JSON -> merged OBJ + anchors + checks.
# --selftest: frozen house fill + check end to end.
nw_add_console_app(
    GROUP apps
    LIBS
        building_geometry_generator
        building_topology_generator
)

add_test(NAME BuildingGeneratorCli_smoke COMMAND BuildingGeneratorCli --selftest)
set_tests_properties(BuildingGeneratorCli_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "check: OK"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
