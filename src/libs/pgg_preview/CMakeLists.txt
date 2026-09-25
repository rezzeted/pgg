# Shared offscreen geometry preview (PggViewer + PggServe). No ImGui.
find_package(glm CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

nw_add_lib(
    GROUP libs/common
    LIBS
        pgg
        glm::glm-header-only
        spdlog::spdlog_header_only
)

find_path(SOKOL_INCLUDE_DIRS "sokol_gfx.h")
if(NOT SOKOL_INCLUDE_DIRS)
    message(FATAL_ERROR "Sokol headers not found (sokol_gfx.h). Make sure vcpkg dependency 'sokol' is installed.")
endif()
target_include_directories(pgg_preview PUBLIC ${SOKOL_INCLUDE_DIRS})

find_path(STB_INCLUDE_DIRS "stb_image.h")
if(NOT STB_INCLUDE_DIRS)
    message(FATAL_ERROR "stb headers not found (stb_image.h). Make sure vcpkg dependency 'stb' is installed.")
endif()
target_include_directories(pgg_preview PRIVATE ${STB_INCLUDE_DIRS})

if(APPLE)
    set_source_files_properties(PreviewCapture.cpp PROPERTIES LANGUAGE OBJCXX)
    target_link_libraries(pgg_preview PRIVATE
        "-framework Metal"
        "-framework Foundation"
        "-framework QuartzCore"
    )
endif()
