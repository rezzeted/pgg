# dungeon_geometry_generator_layout: F2 catalog + F3 generation (dungeon_topology_generator types live here, not in dungeon_geometry_generator).
add_library(dungeon_geometry_generator_layout STATIC
    pch.h
    catalog.h
    catalog.cpp
    generate.h
    generate.cpp
)
target_include_directories(dungeon_geometry_generator_layout PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_precompile_headers(dungeon_geometry_generator_layout PRIVATE pch.h)
target_link_libraries(dungeon_geometry_generator_layout PUBLIC dungeon_geometry_generator dungeon_topology_generator)
target_compile_features(dungeon_geometry_generator_layout PUBLIC cxx_std_20)
set_property(TARGET dungeon_geometry_generator_layout PROPERTY FOLDER libs)
