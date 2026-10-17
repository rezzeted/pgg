# building_topology_generator: building program -> massing/storeys/cores/plans
# -> Building IR (docs/building_generator/requirements.md). B0: IR schema,
# frozen-IR IO, roof planes via geom2d (one roof for topology and geometry,
# section 11.4), topology/geometry checks.
find_package(nlohmann_json CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

add_library(building_topology_generator STATIC
    pch.h
    codes.h
    codes.cpp
    ir.h
    ir.cpp
    massing.h
    massing.cpp
    anneal.h
    anneal.cpp
    cost.h
    cost.cpp
    chimneys.h
    chimneys.cpp
    emit.h
    emit.cpp
    openings.h
    openings.cpp
    pin.h
    pin.cpp
    plan.h
    plan.cpp
    plan_svg.h
    plan_svg.cpp
    pipeline.h
    pipeline.cpp
    program.h
    program.cpp
    rng.h
    roof.h
    roof.cpp
    stairs.h
    stairs.cpp
    storeys.h
    storeys.cpp
)
target_include_directories(building_topology_generator PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_precompile_headers(building_topology_generator PRIVATE pch.h)
target_compile_features(building_topology_generator PUBLIC cxx_std_20)
target_link_libraries(building_topology_generator PUBLIC
    geom2d
    glm::glm-header-only
    nlohmann_json::nlohmann_json
)
set_property(TARGET building_topology_generator PROPERTY FOLDER libs)
