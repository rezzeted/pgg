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

# generate smoke: house program on a 16x14 plot -> ir.json + report + plans.
# The winning shape is a scorer decision (currently the L family); the smoke
# asserts the pipeline runs and reports a choice, not the choice itself.
add_test(NAME BuildingGeneratorCli_smoke_generate
    COMMAND BuildingGeneratorCli generate
        --program "${CMAKE_SOURCE_DIR}/content/building/programs/house.json"
        --w 16 --d 14 --seed 1
        -o "${CMAKE_CURRENT_BINARY_DIR}/smoke_generate")
set_tests_properties(BuildingGeneratorCli_smoke_generate PROPERTIES
    PASS_REGULAR_EXPRESSION "massing: (rect|l|rect\\+annex)"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
