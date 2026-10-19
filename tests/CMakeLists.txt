# SPDX-License-Identifier: AGPL-3.0-only
# LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
include(Catch)

# RT harness (NFR-001, plan decision 1): replaces the global operator new/delete in every test executable and counts
# allocations, deallocations and klay::rt::Mutex locks inside RT_GUARD scopes. JUCE-free.
add_library(klay_rt_harness OBJECT
    rt/RtHarness.cpp
    rt/RtHarness.h)
target_include_directories(klay_rt_harness PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_features(klay_rt_harness PUBLIC cxx_std_20)

# JUCE-free core tests (C-007): links only klay_core, the RT harness and Catch2, so the core is proven to build and
# run without JUCE.
add_executable(klay_core_tests
    core/StateHeaderTests.cpp
    core/RtHarnessTests.cpp)
target_link_libraries(klay_core_tests PRIVATE klay_core klay_rt_harness Catch2::Catch2WithMain)

# Plugin tests: link the plugin's shared-code library (KLAY), which already contains the compiled JUCE modules.
# Its include paths and JucePlugin_* definitions are borrowed instead of linking the JUCE modules a second time.
add_executable(klay_tests
    plugin/ProcessorTests.cpp)
target_link_libraries(klay_tests PRIVATE klay_core klay_rt_harness KLAY Catch2::Catch2WithMain)
target_include_directories(klay_tests PRIVATE
    ${PROJECT_SOURCE_DIR}
    $<TARGET_PROPERTY:KLAY,INCLUDE_DIRECTORIES>)
target_compile_definitions(klay_tests PRIVATE
    $<TARGET_PROPERTY:KLAY,COMPILE_DEFINITIONS>)

foreach(target klay_rt_harness klay_core_tests klay_tests)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8)
    endif()
    set_target_properties(${target} PROPERTIES FOLDER tests)
endforeach()

foreach(target klay_core_tests klay_tests)
    catch_discover_tests(${target} DISCOVERY_MODE PRE_TEST TEST_PREFIX "${target}: ")
endforeach()

# C-007: core/ must not include JUCE headers.
add_test(NAME "core is JUCE-free"
    COMMAND ${CMAKE_COMMAND} -DCORE_DIR=${PROJECT_SOURCE_DIR}/core -P ${PROJECT_SOURCE_DIR}/cmake/CheckCoreJuceFree.cmake)

# C-006 licence labelling check (tools/check-licenses.ps1): the real tree passes, every fixture fails with its own
# violation. PASS_REGULAR_EXPRESSION keeps a crashing script from counting as an expected failure.
find_program(KLAY_POWERSHELL NAMES powershell pwsh REQUIRED)
set(licence_check ${KLAY_POWERSHELL} -NoProfile -ExecutionPolicy Bypass -File ${PROJECT_SOURCE_DIR}/tools/check-licenses.ps1)
set(licence_fixtures ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/licenses)

add_test(NAME "licence check: repository passes" COMMAND ${licence_check})
add_test(NAME "licence check: clean fixture passes" COMMAND ${licence_check} -Root ${licence_fixtures}/clean)
set_tests_properties("licence check: clean fixture passes" PROPERTIES PASS_REGULAR_EXPRESSION "licence check passed: 3 code file")

add_test(NAME "licence check: unmarked GPL file fails" COMMAND ${licence_check} -Root ${licence_fixtures}/unmarked)
set_tests_properties("licence check: unmarked GPL file fails" PROPERTIES
    PASS_REGULAR_EXPRESSION "FAIL: src/unmarked.cpp: .* missing LICENSE-GPL marker")

add_test(NAME "licence check: unlisted GPL file fails" COMMAND ${licence_check} -Root ${licence_fixtures}/unlisted)
set_tests_properties("licence check: unlisted GPL file fails" PROPERTIES
    PASS_REGULAR_EXPRESSION "FAIL: other/unlisted.cpp: .* not listed in THIRD_PARTY_LICENSES.md")

add_test(NAME "licence check: undeclared dependency fails" COMMAND ${licence_check} -Root ${licence_fixtures}/undeclared-dep)
set_tests_properties("licence check: undeclared dependency fails" PROPERTIES
    PASS_REGULAR_EXPRESSION "FAIL: CMakeLists.txt: dependency 'somegpllib'")

add_test(NAME "licence check: missing SPDX header fails" COMMAND ${licence_check} -Root ${licence_fixtures}/no-spdx)
set_tests_properties("licence check: missing SPDX header fails" PROPERTIES
    PASS_REGULAR_EXPRESSION "FAIL: src/no_header.cpp: no SPDX-License-Identifier")
