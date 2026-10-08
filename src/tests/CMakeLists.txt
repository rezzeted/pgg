find_package(GTest REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/pgg/*_test.cpp"
)

add_executable(pgg_tests ${TEST_SOURCES})

target_include_directories(pgg_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/libs
)

target_link_libraries(pgg_tests PRIVATE
    GTest::gtest_main
    pgg
)

target_compile_definitions(pgg_tests PRIVATE
    PGG_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/pgg/corpus"
    PGG_GOLDENS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/pgg/goldens"
    PGG_RESOURCES_DIR="${CMAKE_SOURCE_DIR}/resources/pgg"
)

if(MSVC)
    target_compile_options(pgg_tests PRIVATE "/MP")
endif()

set_property(TARGET pgg_tests PROPERTY FOLDER tests)

add_test(NAME pgg_tests COMMAND pgg_tests)

add_executable(dungeon_geometry_generator_d0_test dungeon_geometry_generator_d0_test.cpp)
target_link_libraries(dungeon_geometry_generator_d0_test PRIVATE GTest::gtest_main dungeon_geometry_generator_d0 pgg nlohmann_json::nlohmann_json)
target_compile_definitions(dungeon_geometry_generator_d0_test PRIVATE DUNGEON_GEOMETRY_GENERATOR_D0_DIR="${CMAKE_SOURCE_DIR}/docs/dungeon_geometry_generator/d0")
target_compile_features(dungeon_geometry_generator_d0_test PRIVATE cxx_std_20)
set_property(TARGET dungeon_geometry_generator_d0_test PROPERTY FOLDER tests)

add_test(NAME dungeon_geometry_generator_d0_test COMMAND dungeon_geometry_generator_d0_test)

add_executable(dungeon_geometry_generator_project_test dungeon_geometry_generator_project_test.cpp)
target_link_libraries(dungeon_geometry_generator_project_test PRIVATE GTest::gtest_main dungeon_geometry_generator)
target_compile_definitions(dungeon_geometry_generator_project_test PRIVATE
    DUNGEON_GEOMETRY_GENERATOR_TEST_DATA="${CMAKE_SOURCE_DIR}/src/tests/data")
target_compile_features(dungeon_geometry_generator_project_test PRIVATE cxx_std_20)
set_property(TARGET dungeon_geometry_generator_project_test PROPERTY FOLDER tests)

add_test(NAME dungeon_geometry_generator_project_test COMMAND dungeon_geometry_generator_project_test)

add_executable(dungeon_geometry_generator_layout_test dungeon_geometry_generator_layout_test.cpp)
target_link_libraries(dungeon_geometry_generator_layout_test PRIVATE GTest::gtest_main dungeon_geometry_generator_layout)
target_compile_definitions(dungeon_geometry_generator_layout_test PRIVATE
    DUNGEON_GEOMETRY_GENERATOR_TEST_DATA="${CMAKE_SOURCE_DIR}/src/tests/data")
target_compile_features(dungeon_geometry_generator_layout_test PRIVATE cxx_std_20)
set_property(TARGET dungeon_geometry_generator_layout_test PROPERTY FOLDER tests)

add_test(NAME dungeon_geometry_generator_layout_test COMMAND dungeon_geometry_generator_layout_test)

add_executable(dungeon_geometry_generator_topo_test dungeon_geometry_generator_topo_test.cpp)
target_link_libraries(dungeon_geometry_generator_topo_test PRIVATE GTest::gtest_main dungeon_geometry_generator)
target_compile_features(dungeon_geometry_generator_topo_test PRIVATE cxx_std_20)
set_property(TARGET dungeon_geometry_generator_topo_test PROPERTY FOLDER tests)

add_test(NAME dungeon_geometry_generator_topo_test COMMAND dungeon_geometry_generator_topo_test)

add_executable(dungeon_geometry_generator_ir_test dungeon_geometry_generator_ir_test.cpp)
target_link_libraries(dungeon_geometry_generator_ir_test PRIVATE GTest::gtest_main dungeon_geometry_generator nlohmann_json::nlohmann_json)
target_compile_definitions(dungeon_geometry_generator_ir_test PRIVATE
    DUNGEON_GEOMETRY_GENERATOR_TEST_DATA="${CMAKE_SOURCE_DIR}/src/tests/data"
    DUNGEON_GEOMETRY_GENERATOR_D0_DIR="${CMAKE_SOURCE_DIR}/docs/dungeon_geometry_generator/d0")
target_compile_features(dungeon_geometry_generator_ir_test PRIVATE cxx_std_20)
set_property(TARGET dungeon_geometry_generator_ir_test PROPERTY FOLDER tests)

add_test(NAME dungeon_geometry_generator_ir_test COMMAND dungeon_geometry_generator_ir_test)

add_executable(dungeon_geometry_generator_assets_test dungeon_geometry_generator_assets_test.cpp)
target_link_libraries(dungeon_geometry_generator_assets_test PRIVATE GTest::gtest_main dungeon_geometry_generator dungeon_geometry_generator_fill pgg nlohmann_json::nlohmann_json)
target_compile_definitions(dungeon_geometry_generator_assets_test PRIVATE DUNGEON_GEOMETRY_GENERATOR_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
target_compile_features(dungeon_geometry_generator_assets_test PRIVATE cxx_std_20)
set_property(TARGET dungeon_geometry_generator_assets_test PROPERTY FOLDER tests)

add_test(NAME dungeon_geometry_generator_assets_test COMMAND dungeon_geometry_generator_assets_test)

add_executable(dungeon_geometry_generator_fill_test dungeon_geometry_generator_fill_test.cpp)
target_link_libraries(dungeon_geometry_generator_fill_test PRIVATE GTest::gtest_main dungeon_geometry_generator_fill nlohmann_json::nlohmann_json)
target_compile_definitions(dungeon_geometry_generator_fill_test PRIVATE
    DUNGEON_GEOMETRY_GENERATOR_TEST_DATA="${CMAKE_SOURCE_DIR}/src/tests/data"
    DUNGEON_GEOMETRY_GENERATOR_D0_DIR="${CMAKE_SOURCE_DIR}/docs/dungeon_geometry_generator/d0"
    DUNGEON_GEOMETRY_GENERATOR_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
target_compile_features(dungeon_geometry_generator_fill_test PRIVATE cxx_std_20)
set_property(TARGET dungeon_geometry_generator_fill_test PROPERTY FOLDER tests)

add_test(NAME dungeon_geometry_generator_fill_test COMMAND dungeon_geometry_generator_fill_test)

add_executable(dungeon_geometry_generator_check_test dungeon_geometry_generator_check_test.cpp)
target_link_libraries(dungeon_geometry_generator_check_test PRIVATE GTest::gtest_main dungeon_geometry_generator_check dungeon_geometry_generator_layout)
target_compile_definitions(dungeon_geometry_generator_check_test PRIVATE
    DUNGEON_GEOMETRY_GENERATOR_TEST_DATA="${CMAKE_SOURCE_DIR}/src/tests/data"
    DUNGEON_GEOMETRY_GENERATOR_D0_DIR="${CMAKE_SOURCE_DIR}/docs/dungeon_geometry_generator/d0"
    DUNGEON_GEOMETRY_GENERATOR_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
target_compile_features(dungeon_geometry_generator_check_test PRIVATE cxx_std_20)
set_property(TARGET dungeon_geometry_generator_check_test PROPERTY FOLDER tests)

add_test(NAME dungeon_geometry_generator_check_test COMMAND dungeon_geometry_generator_check_test)

add_executable(dungeon_geometry_generator_export_test dungeon_geometry_generator_export_test.cpp)
target_link_libraries(dungeon_geometry_generator_export_test PRIVATE GTest::gtest_main dungeon_geometry_generator_export nlohmann_json::nlohmann_json)
target_compile_definitions(dungeon_geometry_generator_export_test PRIVATE
    DUNGEON_GEOMETRY_GENERATOR_TEST_DATA="${CMAKE_SOURCE_DIR}/src/tests/data"
    DUNGEON_GEOMETRY_GENERATOR_D0_DIR="${CMAKE_SOURCE_DIR}/docs/dungeon_geometry_generator/d0"
    DUNGEON_GEOMETRY_GENERATOR_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
target_compile_features(dungeon_geometry_generator_export_test PRIVATE cxx_std_20)
set_property(TARGET dungeon_geometry_generator_export_test PROPERTY FOLDER tests)

add_test(NAME dungeon_geometry_generator_export_test COMMAND dungeon_geometry_generator_export_test)

# --- level-synth (dungeon_topology_generator) tests ---
include(GoogleTest)

add_executable(dungeon_topology_generator_tests dungeon_topology_generator_tests.cpp)
target_link_libraries(dungeon_topology_generator_tests PRIVATE dungeon_topology_generator GTest::gtest_main)
gtest_discover_tests(dungeon_topology_generator_tests)

add_executable(dungeon_topology_generator_parity_tests dungeon_topology_generator_parity_tests.cpp)
target_link_libraries(dungeon_topology_generator_parity_tests PRIVATE dungeon_topology_generator GTest::gtest_main)
gtest_discover_tests(dungeon_topology_generator_parity_tests)

find_package(yaml-cpp CONFIG REQUIRED)
add_executable(preset_loader_tests preset_loader_tests.cpp ../apps/main/preset_loader.cpp)
target_include_directories(preset_loader_tests PRIVATE "${CMAKE_SOURCE_DIR}/src/apps/main")
target_link_libraries(preset_loader_tests PRIVATE dungeon_topology_generator yaml-cpp::yaml-cpp GTest::gtest_main)
gtest_discover_tests(preset_loader_tests)

add_executable(generation_diagnostic_test generation_diagnostic_test.cpp ../apps/main/preset_loader.cpp)
target_include_directories(generation_diagnostic_test PRIVATE "${CMAKE_SOURCE_DIR}/src/apps/main")
target_link_libraries(generation_diagnostic_test PRIVATE dungeon_topology_generator yaml-cpp::yaml-cpp spdlog::spdlog GTest::gtest_main)
gtest_discover_tests(generation_diagnostic_test)

# Golden parity with the reference C# (logical comparison; see test_data/parity/README.md)
add_executable(parity_golden_test parity_golden_test.cpp)
target_link_libraries(parity_golden_test PRIVATE dungeon_topology_generator nlohmann_json::nlohmann_json GTest::gtest_main)
target_compile_definitions(parity_golden_test PRIVATE PARITY_DATA_DIR="${CMAKE_SOURCE_DIR}/test_data/parity")
gtest_discover_tests(parity_golden_test)

# Layout generation benchmark (cross-platform replacement for tools/benchmark_layout_generation.ps1)
add_executable(benchmark_layout benchmark_layout.cpp ../apps/main/preset_loader.cpp)
target_include_directories(benchmark_layout PRIVATE "${CMAKE_SOURCE_DIR}/src/apps/main")
target_link_libraries(benchmark_layout PRIVATE dungeon_topology_generator yaml-cpp::yaml-cpp nlohmann_json::nlohmann_json)
# Smoke gate with a generous threshold; finer thresholds via tools/benchmark_layout_generation.py
# NOTE: dense maps (9/17/41vertices, corridors) do not converge in reasonable time yet —
# tracked as a separate performance investigation; see docs/level-synth/parity_next_steps_plan.md (этап H).
add_test(NAME BenchmarkLayout.Smoke COMMAND benchmark_layout --map tutorial_basic.yml --iterations 5 --threshold-ms 30000)
