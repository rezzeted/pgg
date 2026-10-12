# village_topology_generator: settlement topology pipeline V1-V6
# (docs/village_topology_generator/requirements.md).
add_library(village_topology_generator STATIC
    pch.h
    terrain.h
    terrain.cpp
    analysis.h
    analysis.cpp
    anchors.h
    anchors.cpp
    roads.h
    roads.cpp
    parcels.h
    parcels.cpp
    placement.h
    placement.cpp
    layout_ir.h
    layout_ir.cpp
)
find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(village_topology_generator PUBLIC nlohmann_json::nlohmann_json)
target_include_directories(village_topology_generator PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_precompile_headers(village_topology_generator PRIVATE pch.h)
target_compile_features(village_topology_generator PUBLIC cxx_std_20)
set_property(TARGET village_topology_generator PROPERTY FOLDER libs)
