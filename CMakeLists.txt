cmake_minimum_required(VERSION 4.4)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/VERSION"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchain-versions.env")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" nioh1fix_version)
string(STRIP "${nioh1fix_version}" nioh1fix_version)
if(NOT nioh1fix_version MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain a semantic version in X.Y.Z form")
endif()

project(Nioh1Fix VERSION "${nioh1fix_version}" LANGUAGES CXX)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchain-versions.env"
    nioh1fix_toolchain_version_lines REGEX "^NIOH1FIX_CLANG_MAJOR=")
list(LENGTH nioh1fix_toolchain_version_lines nioh1fix_clang_version_line_count)
if(NOT nioh1fix_clang_version_line_count EQUAL 1)
    message(FATAL_ERROR
        "cmake/toolchain-versions.env must define NIOH1FIX_CLANG_MAJOR once")
endif()
list(GET nioh1fix_toolchain_version_lines 0 nioh1fix_clang_version_line)
string(REPLACE "NIOH1FIX_CLANG_MAJOR=" "" nioh1fix_clang_major
    "${nioh1fix_clang_version_line}")

option(NIOH1FIX_REQUIRE_CLANG_MAJOR
    "Require the Clang major version from the toolchain manifest" OFF)
string(REGEX MATCH "^[0-9]+" nioh1fix_compiler_major
    "${CMAKE_CXX_COMPILER_VERSION}")
if(NIOH1FIX_REQUIRE_CLANG_MAJOR AND
   (NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
    NOT "${nioh1fix_compiler_major}" STREQUAL "${nioh1fix_clang_major}"))
    message(FATAL_ERROR
        "This preset requires Clang major ${nioh1fix_clang_major}; "
        "CMake detected ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
endif()

include(CTest)

file(GLOB nioh1fix_source_files CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
)
list(LENGTH nioh1fix_source_files nioh1fix_source_count)
if(nioh1fix_source_count LESS 7)
    message(FATAL_ERROR "src/ must remain split into focused source files")
endif()
foreach(source_file IN LISTS nioh1fix_source_files)
    file(READ "${source_file}" source_contents)
    string(REGEX MATCHALL "\n" source_line_breaks "${source_contents}")
    list(LENGTH source_line_breaks source_line_count)
    # This is a decomposition threshold: split by responsibility instead of
    # compressing readable code to fit beneath it.
    if(source_line_count GREATER 150)
        message(FATAL_ERROR "${source_file} exceeds the 150-line limit")
    endif()
endforeach()

add_library(nioh1fix_core INTERFACE)
target_include_directories(nioh1fix_core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(nioh1fix_core INTERFACE cxx_std_20)

if(WIN32)
    add_library(Nioh1Fix SHARED
        src/action_input.cpp
        src/compatibility.cpp
        src/dllmain.cpp
        src/hooks.cpp
        src/monitor.cpp
        src/patches.cpp
        src/platform.cpp
        src/timing.cpp
    )
    target_link_libraries(Nioh1Fix PRIVATE nioh1fix_core)
    target_compile_definitions(Nioh1Fix PRIVATE
        NOMINMAX
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NIOH1FIX_VERSION="${PROJECT_VERSION}"
    )
    set_target_properties(Nioh1Fix PROPERTIES
        PREFIX ""
        SUFFIX ".asi"
    )

    if(MSVC)
        set_property(TARGET Nioh1Fix PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_compile_options(Nioh1Fix PRIVATE /W4 /permissive-)
    elseif(MINGW)
        target_compile_options(Nioh1Fix PRIVATE -Wall -Wextra -Wpedantic)
        target_link_options(Nioh1Fix PRIVATE -static -static-libgcc -static-libstdc++)
    endif()

    install(TARGETS Nioh1Fix RUNTIME DESTINATION . LIBRARY DESTINATION .)
    install(FILES Nioh1Fix.ini DESTINATION .)
endif()

if(BUILD_TESTING)
    add_executable(frame_profile_tests tests/frame_profile_tests.cpp)
    target_link_libraries(frame_profile_tests PRIVATE nioh1fix_core)
    add_test(NAME frame_profile_tests COMMAND frame_profile_tests)

    add_executable(timing_scale_tests tests/timing_scale_tests.cpp)
    target_link_libraries(timing_scale_tests PRIVATE nioh1fix_core)
    add_test(NAME timing_scale_tests COMMAND timing_scale_tests)

    add_executable(input_timing_tests tests/input_timing_tests.cpp)
    target_link_libraries(input_timing_tests PRIVATE nioh1fix_core)
    add_test(NAME input_timing_tests COMMAND input_timing_tests)
endif()
