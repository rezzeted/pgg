find_package(yaml-cpp CONFIG REQUIRED)

set(MAIN_SOURCES main.cpp layout_preview.cpp preset_loader.cpp app_ui.cpp app_state.cpp)
if(WIN32)
    list(APPEND MAIN_SOURCES file_dialogs.cpp)
endif()

add_executable(main ${MAIN_SOURCES})
target_link_libraries(main PRIVATE imgui_impl edgar yaml-cpp::yaml-cpp drui)
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

# Bundle Edgar.GUI-style assets (mirrors Edgar-DotNet `src/Resources`: MapDescriptions, Maps, etc.)
set(LEVELSYNTH_EDGAR_GUI_RESOURCES "${CMAKE_SOURCE_DIR}/resources/edgar_gui")
if(EXISTS "${LEVELSYNTH_EDGAR_GUI_RESOURCES}")
    add_custom_command(
        TARGET main
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${LEVELSYNTH_EDGAR_GUI_RESOURCES}"
                "$<TARGET_FILE_DIR:main>/resources/edgar_gui"
        COMMENT "Copy resources/edgar_gui next to main (MapDescriptions, Images, etc.)")
endif()
