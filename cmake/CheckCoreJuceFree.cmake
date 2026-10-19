# SPDX-License-Identifier: AGPL-3.0-only
# LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#
# C-007: fails if any file under CORE_DIR includes a JUCE header or uses the juce:: namespace.
# Usage: cmake -DCORE_DIR=<path> -P CheckCoreJuceFree.cmake
if(NOT CORE_DIR)
    message(FATAL_ERROR "CORE_DIR not set")
endif()

file(GLOB_RECURSE files "${CORE_DIR}/*.h" "${CORE_DIR}/*.hpp" "${CORE_DIR}/*.cpp" "${CORE_DIR}/*.inl")
set(offenders "")
foreach(f IN LISTS files)
    file(STRINGS "${f}" hits REGEX "#[ \t]*include[ \t]*[<\"]juce_|juce::")
    if(hits)
        list(APPEND offenders "${f}")
    endif()
endforeach()

list(LENGTH files count)
if(offenders)
    list(JOIN offenders "\n  " text)
    message(FATAL_ERROR "C-007 violated, JUCE used in core/:\n  ${text}")
endif()
message(STATUS "core is JUCE-free (${count} files checked)")
