# geom2d: domain-neutral 2D geometry (straight skeleton, outline offset).
# Extracted from the pgg kernel per docs/building_generator/requirements.md
# (section 11.4) so both the PGG builtin and building_topology_generator share
# the same roof planes.
find_package(glm CONFIG REQUIRED)

add_library(geom2d STATIC
    pch.h
    straight_skeleton.h
    straight_skeleton.cpp
)
target_include_directories(geom2d PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_precompile_headers(geom2d PRIVATE pch.h)
target_compile_features(geom2d PUBLIC cxx_std_20)
target_link_libraries(geom2d PUBLIC glm::glm-header-only)
set_property(TARGET geom2d PROPERTY FOLDER libs)
