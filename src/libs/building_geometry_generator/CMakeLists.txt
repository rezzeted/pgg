# building_geometry_generator: Building IR -> PGG slot assets -> merged mesh +
# anchors + checks (docs/building_generator/requirements.md section 11, B0).
find_package(nlohmann_json CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

add_library(building_geometry_generator STATIC
    pch.h
    building_geometry.h
    building_geometry.cpp
    unit.h
    unit.cpp
    check.cpp
    emit.cpp
)
target_include_directories(building_geometry_generator PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_precompile_headers(building_geometry_generator PRIVATE pch.h)
target_compile_features(building_geometry_generator PUBLIC cxx_std_20)
target_link_libraries(building_geometry_generator PUBLIC
    building_topology_generator
    pgg
    nlohmann_json::nlohmann_json
    glm::glm-header-only
)
set_property(TARGET building_geometry_generator PROPERTY FOLDER libs)
