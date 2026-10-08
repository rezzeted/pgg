add_library(dungeon_geometry_generator_fill STATIC
    fill_ra3.cpp
    fill.cpp
    cache.cpp
    assets.cpp
)

target_include_directories(dungeon_geometry_generator_fill PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dungeon_geometry_generator_fill PUBLIC dungeon_geometry_generator pgg)
target_compile_features(dungeon_geometry_generator_fill PUBLIC cxx_std_20)
