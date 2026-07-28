cmake_minimum_required(VERSION 3.21)

project(Nioh1Fix VERSION 0.9.0 LANGUAGES CXX)

include(CTest)

add_library(frame_profile INTERFACE)
target_include_directories(frame_profile INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(frame_profile INTERFACE cxx_std_20)

if(WIN32)
    add_library(Nioh1Fix SHARED src/dllmain.cpp)
    target_link_libraries(Nioh1Fix PRIVATE frame_profile)
    target_compile_definitions(Nioh1Fix PRIVATE
        NOMINMAX
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
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
    target_link_libraries(frame_profile_tests PRIVATE frame_profile)
    add_test(NAME frame_profile_tests COMMAND frame_profile_tests)
endif()
