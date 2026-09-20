find_package(GTest REQUIRED)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/pgg/*_test.cpp"
)

add_executable(pgg_tests ${TEST_SOURCES})

target_include_directories(pgg_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/libs
)

target_link_libraries(pgg_tests PRIVATE
    GTest::gtest_main
    pgg
)

target_compile_definitions(pgg_tests PRIVATE
    PGG_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/pgg/corpus"
    PGG_GOLDENS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/pgg/goldens"
    PGG_RESOURCES_DIR="${CMAKE_SOURCE_DIR}/resources/pgg"
)

if(MSVC)
    target_compile_options(pgg_tests PRIVATE "/MP")
endif()

set_property(TARGET pgg_tests PROPERTY FOLDER tests)

add_test(NAME pgg_tests COMMAND pgg_tests)
