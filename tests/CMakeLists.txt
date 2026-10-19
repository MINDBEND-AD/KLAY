# SPDX-License-Identifier: AGPL-3.0-only
# LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
include(Catch)

# RT harness (NFR-001, plan decision 1): replaces the global operator new/delete in every test executable and counts
# allocations, deallocations and klay::rt::Mutex locks inside RT_GUARD scopes; on Windows it also hooks OS heap, lock,
# file I/O, logging and sleep calls. JUCE-free.
add_library(klay_rt_harness OBJECT
    rt/RtHarness.cpp
    rt/RtHarness.h)
target_include_directories(klay_rt_harness PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_features(klay_rt_harness PUBLIC cxx_std_20)
if(WIN32)
    target_link_libraries(klay_rt_harness PUBLIC Synchronization) # WaitOnAddress
endif()

# JUCE-free core tests (C-007): links only klay_core, the RT harness and Catch2, so the core is proven to build and
# run without JUCE.
add_executable(klay_core_tests
    core/CacheTests.cpp
    core/KeyDetectionTests.cpp
    core/KickModelTests.cpp
    core/PlayerTests.cpp
    core/RenderTests.cpp
    core/StretchTests.cpp
    core/TestSignals.h
    core/TimeEnvelopeTests.cpp
    core/RtHarnessTests.cpp)
target_link_libraries(klay_core_tests PRIVATE klay_core klay_rt_harness Catch2::Catch2WithMain)

# Plugin tests: link the plugin's shared-code library (KLAY), which already contains the compiled JUCE modules.
# Its include paths and JucePlugin_* definitions are borrowed instead of linking the JUCE modules a second time.
add_executable(klay_tests
    plugin/AdapterTests.cpp
    plugin/AssumptionChecks.cpp
    plugin/EditorTests.cpp
    plugin/PlaybackTests.cpp
    plugin/ProcessorTests.cpp)
target_link_libraries(klay_tests PRIVATE klay_core klay_rt_harness KLAY Catch2::Catch2WithMain)
target_include_directories(klay_tests PRIVATE
    ${PROJECT_SOURCE_DIR}
    $<TARGET_PROPERTY:KLAY,INCLUDE_DIRECTORIES>)
target_compile_definitions(klay_tests PRIVATE
    KLAY_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
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
foreach(fixture juceheader juce-macro)
    add_test(NAME "core JUCE check: ${fixture} fixture fails"
        COMMAND ${CMAKE_COMMAND} -DCORE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/core-juce/${fixture}
                -P ${PROJECT_SOURCE_DIR}/cmake/CheckCoreJuceFree.cmake)
    set_tests_properties("core JUCE check: ${fixture} fixture fails" PROPERTIES
        PASS_REGULAR_EXPRESSION "C-007 violated, JUCE used in core/:.*${fixture}/Bad.h")
endforeach()

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

add_test(NAME "licence check: other code extensions are scanned" COMMAND ${licence_check} -Root ${licence_fixtures}/other-extensions)
set_tests_properties("licence check: other code extensions are scanned" PROPERTIES
    PASS_REGULAR_EXPRESSION "licence check failed: 9 violation[(]s[)] in 9 code file")

add_test(NAME "licence check: SPDX licence not matching its row fails" COMMAND ${licence_check} -Root ${licence_fixtures}/licence-mismatch)
set_tests_properties("licence check: SPDX licence not matching its row fails" PROPERTIES
    PASS_REGULAR_EXPRESSION "FAIL: src/gpl2.cpp: SPDX licence [(]GPL-2.0-only[)] does not match .*'Own code'.*licence check failed: 1 violation")

add_test(NAME "licence check: dependency name prefix of a component fails" COMMAND ${licence_check} -Root ${licence_fixtures}/prefix-dep)
set_tests_properties("licence check: dependency name prefix of a component fails" PROPERTIES
    PASS_REGULAR_EXPRESSION "FAIL: CMakeLists.txt: dependency 'JUC'")

add_test(NAME "licence check: 8.3 short root passes"
    COMMAND ${KLAY_POWERSHELL} -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-licenses-short-root.ps1
            -Fixture ${licence_fixtures}/short-path-root)
set_tests_properties("licence check: 8.3 short root passes" PROPERTIES
    PASS_REGULAR_EXPRESSION "licence check passed: 1 code file" SKIP_RETURN_CODE 77)

# Golden-file null tests (§12, task 1.14): each fixture kick renders within −120 dBFS of its stored WAV. The parts
# under fixtures/kicks/samples come from `klay-render fixtures`; regenerate the goldens with `klay-render render`
# only on purpose (a deliberate change to the render) and say so in the commit.
foreach(kick punch stack repitch)
    add_test(NAME "golden: ${kick} kick nulls against its reference"
        COMMAND klay_render_cli render
                --kick ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/kicks/${kick}.json
                --out ${CMAKE_CURRENT_BINARY_DIR}/golden-${kick}.wav --sr 48000
                --compare ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/kicks/golden/${kick}.wav)
endforeach()

# The null test must fail for a changed kick (0.1 dB on one fader) and for a different length.
add_test(NAME "golden: a 0.1 dB fader change fails the null test"
    COMMAND klay_render_cli render --kick ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/kicks/punch-variant.json
            --out ${CMAKE_CURRENT_BINARY_DIR}/golden-variant.wav --sr 48000
            --compare ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/kicks/golden/punch.wav)
set_tests_properties("golden: a 0.1 dB fader change fails the null test" PROPERTIES WILL_FAIL TRUE)
add_test(NAME "golden: a different length fails the null test"
    COMMAND klay_render_cli render --kick ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/kicks/stack.json
            --out ${CMAKE_CURRENT_BINARY_DIR}/golden-length.wav --sr 48000
            --compare ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/kicks/golden/punch.wav)
set_tests_properties("golden: a different length fails the null test" PROPERTIES WILL_FAIL TRUE)

# Task 1.17: the listening-material generator runs end to end on the synthetic fixture parts.
add_test(NAME "listening material generator runs on the fixture parts"
    COMMAND klay_render_cli listening --samples ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/kicks/samples
            --out ${CMAKE_CURRENT_BINARY_DIR}/listening-smoke)
set_tests_properties("listening material generator runs on the fixture parts" PROPERTIES
    PASS_REGULAR_EXPRESSION "listening material: 4 parts, 136 files"
    FAIL_REGULAR_EXPRESSION "cannot write")

# Task 1.21 (NFR-012): spec v0.4 kicks (schema 1) still render identically through the migration.
foreach(kick stack repitch)
    add_test(NAME "golden: schema 1 ${kick} kick migrates and nulls"
        COMMAND klay_render_cli render
                --kick ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/kicks/v1/${kick}.json
                --out ${CMAKE_CURRENT_BINARY_DIR}/golden-v1-${kick}.wav --sr 48000
                --compare ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/kicks/golden/${kick}.wav)
endforeach()
