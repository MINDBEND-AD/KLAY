# SPDX-License-Identifier: AGPL-3.0-only
# LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
include(Catch)

add_executable(klay_tests
    core/StateHeaderTests.cpp
    plugin/ProcessorTests.cpp)

# The processor tests link the plugin's shared-code library (KLAY), which already contains the compiled JUCE
# modules. Its include paths and JucePlugin_* definitions are borrowed instead of linking the JUCE modules a
# second time.
target_link_libraries(klay_tests PRIVATE klay_core KLAY Catch2::Catch2WithMain)
target_include_directories(klay_tests PRIVATE
    ${PROJECT_SOURCE_DIR}
    $<TARGET_PROPERTY:KLAY,INCLUDE_DIRECTORIES>)
target_compile_definitions(klay_tests PRIVATE
    $<TARGET_PROPERTY:KLAY,COMPILE_DEFINITIONS>)

if(MSVC)
    target_compile_options(klay_tests PRIVATE /utf-8)
endif()

catch_discover_tests(klay_tests DISCOVERY_MODE PRE_TEST)
