find_package(SDL3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

# Interface target keeps the viewer linking unchanged (imgui + OpenGL)
add_library(vv_imgui_impl INTERFACE)
target_link_libraries(vv_imgui_impl INTERFACE imgui::imgui)
if(WIN32)
    target_link_libraries(vv_imgui_impl INTERFACE opengl32)
elseif(APPLE)
    target_compile_definitions(vv_imgui_impl INTERFACE GL_SILENCE_DEPRECATION)
    find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
    target_link_libraries(vv_imgui_impl INTERFACE ${OPENGL_FRAMEWORK})
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(vv_imgui_impl INTERFACE OpenGL::GL)
endif()

add_executable(VillageTopologyGeneratorViewer main.cpp app_ui.cpp map_preview.cpp)
target_link_libraries(VillageTopologyGeneratorViewer PRIVATE vv_imgui_impl village_topology_generator drui)
target_include_directories(VillageTopologyGeneratorViewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET VillageTopologyGeneratorViewer PROPERTY FOLDER apps)

set(VV_FONT_RESOURCES "${CMAKE_SOURCE_DIR}/resources/fonts")
if(EXISTS "${VV_FONT_RESOURCES}")
    add_custom_command(
        TARGET VillageTopologyGeneratorViewer
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:VillageTopologyGeneratorViewer>/resources/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${VV_FONT_RESOURCES}"
                "$<TARGET_FILE_DIR:VillageTopologyGeneratorViewer>/resources/fonts"
        COMMENT "Copy resources/fonts next to VillageTopologyGeneratorViewer (Font Awesome optional)")
endif()

# Headless smoke: runs the full pipeline without opening a window.
add_test(NAME VillageTopologyGeneratorViewer_smoke COMMAND VillageTopologyGeneratorViewer --smoke)
set_tests_properties(VillageTopologyGeneratorViewer_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "VillageTopologyGeneratorViewer smoke ok")
