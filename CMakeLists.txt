cmake_minimum_required(VERSION 3.20.0 FATAL_ERROR)
include(cmake/utils.cmake)

project(pgg VERSION 0.1.0 LANGUAGES CXX C)
if(APPLE)
    # Standalone sokol-приложения компилируют main.cpp как Objective-C++
    # (nw_configure_sokol_app); Ninja/Makefile-генераторы требуют явного
    # включения языка (Xcode включает его сам).
    enable_language(OBJCXX)
endif()
print_header("pgg")
check_preprocessor_configuration()
configure_common_param()
if(MSVC)
    add_compile_options(/MP)
endif()

#use grouping
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE)
    set(OpenGL_GL_PREFERENCE "GLVND" CACHE STRING "" FORCE)
    set(OPENGL_INCLUDE_DIR "/System/Library/Frameworks/OpenGL.framework/Headers" CACHE PATH "" FORCE)
    set(OPENGL_gl_LIBRARY "/System/Library/Frameworks/OpenGL.framework/OpenGL" CACHE FILEPATH "" FORCE)
elseif(UNIX)
    set(OpenGL_GL_PREFERENCE "GLVND" CACHE STRING "" FORCE)
endif()

find_package(spdlog CONFIG REQUIRED)

enable_testing()

add_subdirectory(src/libs)
add_subdirectory(src/apps)
add_subdirectory(src/tests)
