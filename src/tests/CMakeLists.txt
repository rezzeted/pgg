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

add_executable(delve_d0_test delve_d0_test.cpp)
target_link_libraries(delve_d0_test PRIVATE GTest::gtest_main delve_d0 pgg nlohmann_json::nlohmann_json)
target_compile_definitions(delve_d0_test PRIVATE DELVE_D0_DIR="${CMAKE_SOURCE_DIR}/docs/delve/d0")
target_compile_features(delve_d0_test PRIVATE cxx_std_20)
set_property(TARGET delve_d0_test PROPERTY FOLDER tests)

add_test(NAME delve_d0_test COMMAND delve_d0_test)

add_executable(delve_project_test delve_project_test.cpp)
target_link_libraries(delve_project_test PRIVATE GTest::gtest_main delve)
target_compile_definitions(delve_project_test PRIVATE
    DELVE_TEST_DATA="${CMAKE_SOURCE_DIR}/src/tests/data")
target_compile_features(delve_project_test PRIVATE cxx_std_20)
set_property(TARGET delve_project_test PROPERTY FOLDER tests)

add_test(NAME delve_project_test COMMAND delve_project_test)

add_executable(delve_layout_test delve_layout_test.cpp)
target_link_libraries(delve_layout_test PRIVATE GTest::gtest_main delve_layout)
target_compile_definitions(delve_layout_test PRIVATE
    DELVE_TEST_DATA="${CMAKE_SOURCE_DIR}/src/tests/data")
target_compile_features(delve_layout_test PRIVATE cxx_std_20)
set_property(TARGET delve_layout_test PROPERTY FOLDER tests)

add_test(NAME delve_layout_test COMMAND delve_layout_test)

add_executable(delve_topo_test delve_topo_test.cpp)
target_link_libraries(delve_topo_test PRIVATE GTest::gtest_main delve)
target_compile_features(delve_topo_test PRIVATE cxx_std_20)
set_property(TARGET delve_topo_test PROPERTY FOLDER tests)

add_test(NAME delve_topo_test COMMAND delve_topo_test)

add_executable(delve_ir_test delve_ir_test.cpp)
target_link_libraries(delve_ir_test PRIVATE GTest::gtest_main delve nlohmann_json::nlohmann_json)
target_compile_definitions(delve_ir_test PRIVATE
    DELVE_TEST_DATA="${CMAKE_SOURCE_DIR}/src/tests/data"
    DELVE_D0_DIR="${CMAKE_SOURCE_DIR}/docs/delve/d0")
target_compile_features(delve_ir_test PRIVATE cxx_std_20)
set_property(TARGET delve_ir_test PROPERTY FOLDER tests)

add_test(NAME delve_ir_test COMMAND delve_ir_test)

add_executable(delve_assets_test delve_assets_test.cpp)
target_link_libraries(delve_assets_test PRIVATE GTest::gtest_main delve delve_fill pgg nlohmann_json::nlohmann_json)
target_compile_definitions(delve_assets_test PRIVATE DELVE_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
target_compile_features(delve_assets_test PRIVATE cxx_std_20)
set_property(TARGET delve_assets_test PROPERTY FOLDER tests)

add_test(NAME delve_assets_test COMMAND delve_assets_test)

add_executable(delve_fill_test delve_fill_test.cpp)
target_link_libraries(delve_fill_test PRIVATE GTest::gtest_main delve_fill nlohmann_json::nlohmann_json)
target_compile_definitions(delve_fill_test PRIVATE
    DELVE_TEST_DATA="${CMAKE_SOURCE_DIR}/src/tests/data"
    DELVE_D0_DIR="${CMAKE_SOURCE_DIR}/docs/delve/d0"
    DELVE_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
target_compile_features(delve_fill_test PRIVATE cxx_std_20)
set_property(TARGET delve_fill_test PROPERTY FOLDER tests)

add_test(NAME delve_fill_test COMMAND delve_fill_test)

add_executable(delve_check_test delve_check_test.cpp)
target_link_libraries(delve_check_test PRIVATE GTest::gtest_main delve_check delve_layout)
target_compile_definitions(delve_check_test PRIVATE
    DELVE_TEST_DATA="${CMAKE_SOURCE_DIR}/src/tests/data"
    DELVE_D0_DIR="${CMAKE_SOURCE_DIR}/docs/delve/d0"
    DELVE_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
target_compile_features(delve_check_test PRIVATE cxx_std_20)
set_property(TARGET delve_check_test PROPERTY FOLDER tests)

add_test(NAME delve_check_test COMMAND delve_check_test)

add_executable(delve_export_test delve_export_test.cpp)
target_link_libraries(delve_export_test PRIVATE GTest::gtest_main delve_export nlohmann_json::nlohmann_json)
target_compile_definitions(delve_export_test PRIVATE
    DELVE_TEST_DATA="${CMAKE_SOURCE_DIR}/src/tests/data"
    DELVE_D0_DIR="${CMAKE_SOURCE_DIR}/docs/delve/d0"
    DELVE_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
target_compile_features(delve_export_test PRIVATE cxx_std_20)
set_property(TARGET delve_export_test PROPERTY FOLDER tests)

add_test(NAME delve_export_test COMMAND delve_export_test)

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
