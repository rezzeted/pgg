# DungeonGeometryGeneratorServe: CPU RPC daemon of the dungeon_geometry_generator pipeline (docs/dungeon_geometry_generator/mcp_v1.md) —
# project slots with the warm F8 unit cache. ServeRpcServer is an adapted copy
# of src/apps/PggServe (line-delimited JSON over TCP); only the dungeon_geometry_generator pipeline
# libs are linked from here.
# nlohmann_json is found at src/libs/dungeon_geometry_generator scope only; re-find it here so the
# imported target is visible (spdlog is already found at the repo root).
find_package(nlohmann_json CONFIG REQUIRED)

nw_add_console_app(
    GROUP apps/tools
    LIBS
        dungeon_geometry_generator
        dungeon_geometry_generator_layout
        dungeon_geometry_generator_fill
        dungeon_geometry_generator_check
        dungeon_geometry_generator_export
        spdlog::spdlog_header_only
        nlohmann_json::nlohmann_json
)

# Smoke: a real server on 127.0.0.1:0 plus real TCP clients. The working
# directory is the repo root so the relative fixture paths and the ./assets
# fallback resolve like in the DungeonGeometryGeneratorCli smokes.
add_test(NAME DungeonGeometryGeneratorServe_smoke COMMAND DungeonGeometryGeneratorServe --smoke)
set_tests_properties(DungeonGeometryGeneratorServe_smoke PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Python wire-protocol smoke (tools/dungeon_geometry_generator_mcp): a real DungeonGeometryGeneratorServe on a free
# port plus a stdlib raw-socket client — guards the Python<->C++ framing.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME DungeonGeometryGeneratorServe_rpc_py COMMAND ${Python3_EXECUTABLE}
           ${CMAKE_SOURCE_DIR}/tools/dungeon_geometry_generator_mcp/tests/integration_smoke.py
           $<TARGET_FILE:DungeonGeometryGeneratorServe> ${CMAKE_SOURCE_DIR})
  set_tests_properties(DungeonGeometryGeneratorServe_rpc_py PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()
