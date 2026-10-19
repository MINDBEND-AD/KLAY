# SPDX-License-Identifier: AGPL-3.0-only
# LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
include(Catch)

# JUCE-free core tests (C-007): links only klay_core and Catch2, so the core is proven to build and run without
# JUCE.
add_executable(klay_core_tests
    core/StateHeaderTests.cpp)
target_link_libraries(klay_core_tests PRIVATE klay_core Catch2::Catch2WithMain)

# Plugin tests: link the plugin's shared-code library (KLAY), which already contains the compiled JUCE modules.
# Its include paths and JucePlugin_* definitions are borrowed instead of linking the JUCE modules a second time.
add_executable(klay_tests
    plugin/ProcessorTests.cpp)
target_link_libraries(klay_tests PRIVATE klay_core KLAY Catch2::Catch2WithMain)
target_include_directories(klay_tests PRIVATE
    ${PROJECT_SOURCE_DIR}
    $<TARGET_PROPERTY:KLAY,INCLUDE_DIRECTORIES>)
target_compile_definitions(klay_tests PRIVATE
    $<TARGET_PROPERTY:KLAY,COMPILE_DEFINITIONS>)

foreach(target klay_core_tests klay_tests)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8)
    endif()
    set_target_properties(${target} PROPERTIES FOLDER tests)
    catch_discover_tests(${target} DISCOVERY_MODE PRE_TEST TEST_PREFIX "${target}: ")
endforeach()

# C-007: core/ must not include JUCE headers.
add_test(NAME "core is JUCE-free"
    COMMAND ${CMAKE_COMMAND} -DCORE_DIR=${PROJECT_SOURCE_DIR}/core -P ${PROJECT_SOURCE_DIR}/cmake/CheckCoreJuceFree.cmake)
