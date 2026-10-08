# Edgar C++ port — core library
cmake_minimum_required(VERSION 3.20)
include(FetchContent)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

set(EDGAR_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE INTERNAL "")

find_package(nlohmann_json CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(Boost REQUIRED COMPONENTS graph CONFIG)

# Clipper2: same version as vcpkg port (2.0.1) but built in-tree so the static
# library matches the same MSVC toolchain as edgar (avoids LNK2019 __std_rotate
# when a prebuilt vcpkg binary was produced with a different VC toolset).
FetchContent_Declare(
    clipper2_upstream
    GIT_REPOSITORY https://github.com/AngusJohnson/Clipper2.git
    GIT_TAG Clipper2_2.0.1
    GIT_SHALLOW TRUE
)
set(CLIPPER2_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CLIPPER2_TESTS OFF CACHE BOOL "" FORCE)
set(CLIPPER2_UTILS OFF CACHE BOOL "" FORCE)
FetchContent_GetProperties(clipper2_upstream)
if(NOT clipper2_upstream_POPULATED)
    FetchContent_Populate(clipper2_upstream)
endif()
add_subdirectory(${clipper2_upstream_SOURCE_DIR}/CPP ${clipper2_upstream_BINARY_DIR} EXCLUDE_FROM_ALL)

add_library(edgar STATIC
    src/geometry/orthogonal_line_grid2d.cpp
    src/geometry/orthogonal_line_intersection.cpp
    src/geometry/bipartite_matching.cpp
    src/geometry/grid_polygon_partitioning.cpp
    src/geometry/polygon_overlap_grid2d.cpp
    src/geometry/polygon_grid2d.cpp
    src/geometry/overlap.cpp
    src/geometry/clipper2_util.cpp
    src/graphs/planar_faces.cpp
    src/generator/grid2d/configuration_spaces_generator.cpp
    src/generator/grid2d/configuration_spaces_grid2d.cpp
    src/generator/grid2d/simulated_annealing_evolver_grid2d.cpp
    src/generator/grid2d/room_template_grid2d.cpp
    src/generator/grid2d/simple_door_mode_grid2d.cpp
    src/generator/grid2d/manual_door_mode_grid2d.cpp
    src/io/dungeon_drawer.cpp
    src/io/png_rgba.cpp
)
target_include_directories(edgar
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${Stb_INCLUDE_DIR}
)
target_link_libraries(edgar
    PUBLIC
        nlohmann_json::nlohmann_json
        Boost::graph
        Clipper2
    PRIVATE
        fmt::fmt
        spdlog::spdlog
)
target_compile_features(edgar PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(edgar PRIVATE /W4 /permissive-)
else()
    target_compile_options(edgar PRIVATE -Wall -Wextra -Wpedantic)
endif()
