find_package(nlohmann_json CONFIG REQUIRED)

add_library(dungeon_geometry_generator STATIC
    project.cpp
    project_writer.cpp
    layout.cpp
    ir.cpp
    topo.cpp
)

target_include_directories(dungeon_geometry_generator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_precompile_headers(dungeon_geometry_generator PRIVATE pch.h)
target_link_libraries(dungeon_geometry_generator PUBLIC nlohmann_json::nlohmann_json)
