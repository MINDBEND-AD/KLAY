# SPDX-License-Identifier: AGPL-3.0-only
# LICENSE-GPL: KLAY own source, built on JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
cmake_minimum_required(VERSION 3.25)

project(KLAY VERSION 0.1.0 LANGUAGES C CXX)

option(KLAY_BUILD_TESTS "Build the klay_tests executable" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static MSVC runtime: the plugin DLL then has no VC++ redistributable dependency in hosts (C-002).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(cmake/Dependencies.cmake)

add_subdirectory(core)
add_subdirectory(plugin)

if(KLAY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
