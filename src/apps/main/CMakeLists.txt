find_package(yaml-cpp CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

# Interface target keeps `main` linking unchanged (imgui + OpenGL)
add_library(imgui_impl INTERFACE)
target_link_libraries(imgui_impl INTERFACE imgui::imgui)
if(WIN32)
    target_link_libraries(imgui_impl INTERFACE opengl32)
elseif(APPLE)
    # Use the system OpenGL framework (not the XQuartz libGL that FindOpenGL may pick)
    target_compile_definitions(imgui_impl INTERFACE GL_SILENCE_DEPRECATION)
    find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
    target_link_libraries(imgui_impl INTERFACE ${OPENGL_FRAMEWORK})
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(imgui_impl INTERFACE OpenGL::GL)
endif()

set(MAIN_SOURCES main.cpp layout_preview.cpp preset_loader.cpp app_ui.cpp app_state.cpp)
if(WIN32)
    list(APPEND MAIN_SOURCES file_dialogs.cpp)
endif()

add_executable(main ${MAIN_SOURCES})
target_link_libraries(main PRIVATE imgui_impl dungeon_topology_generator yaml-cpp::yaml-cpp drui)
target_include_directories(main PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set(LEVELSYNTH_FONT_RESOURCES "${CMAKE_SOURCE_DIR}/resources/fonts")
if(EXISTS "${LEVELSYNTH_FONT_RESOURCES}")
    add_custom_command(
        TARGET main
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:main>/resources/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${LEVELSYNTH_FONT_RESOURCES}"
                "$<TARGET_FILE_DIR:main>/resources/fonts"
        COMMENT "Copy resources/fonts next to main (Font Awesome optional)")
endif()

# Bundle Edgar.GUI-style assets (trimmed copy of Edgar-DotNet `src/Resources`: Maps, Rooms, Images)
set(LEVELSYNTH_DUNGEON_TOPOLOGY_GENERATOR_GUI_RESOURCES "${CMAKE_SOURCE_DIR}/resources/dungeon_topology_generator_gui")
if(EXISTS "${LEVELSYNTH_DUNGEON_TOPOLOGY_GENERATOR_GUI_RESOURCES}")
    add_custom_command(
        TARGET main
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${LEVELSYNTH_DUNGEON_TOPOLOGY_GENERATOR_GUI_RESOURCES}"
                "$<TARGET_FILE_DIR:main>/resources/dungeon_topology_generator_gui"
        COMMENT "Copy resources/dungeon_topology_generator_gui next to main (Maps, Rooms, Images)")
endif()
