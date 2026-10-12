# village_geometry_generator: Layout IR -> PGG building assets -> merged mesh
# (docs/village_topology_generator/requirements.md §10, S6).
find_package(nlohmann_json CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

add_library(village_geometry_generator STATIC
    pch.h
    village_geometry.h
    village_geometry.cpp
)
target_include_directories(village_geometry_generator PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_precompile_headers(village_geometry_generator PRIVATE pch.h)
target_compile_features(village_geometry_generator PUBLIC cxx_std_20)
target_link_libraries(village_geometry_generator PUBLIC pgg nlohmann_json::nlohmann_json
                      PRIVATE glm::glm-header-only)
set_property(TARGET village_geometry_generator PROPERTY FOLDER libs)
