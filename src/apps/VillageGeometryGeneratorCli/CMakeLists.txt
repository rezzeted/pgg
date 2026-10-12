# VillageGeometryGeneratorCli (S6): Layout IR JSON -> merged OBJ.
# --selftest: topology pipeline + geometry end to end, no IO.
nw_add_console_app(
    GROUP apps
    LIBS
        village_geometry_generator
        village_topology_generator
)

add_test(NAME VillageGeometryGeneratorCli_smoke COMMAND VillageGeometryGeneratorCli --selftest)
set_tests_properties(VillageGeometryGeneratorCli_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "buildings: [1-9][0-9]*/[1-9][0-9]* built"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
