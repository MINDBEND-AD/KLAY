# SPDX-License-Identifier: AGPL-3.0-only
# LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#
# Builds the Steinberg VST3 validator from the pinned VST3 SDK (NFR-011, plan decision 18). Separate from the
# plugin build: the plugin uses the SDK copy bundled with JUCE; this project only produces validator.exe.
# Used by tools/validate.ps1.
cmake_minimum_required(VERSION 3.25)
project(klay_vst3_validator LANGUAGES C CXX)

include(FetchContent)

set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_ADD_VSTGUI OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES ON CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)

FetchContent_Declare(vst3sdk
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
    GIT_TAG        v3.8.0_build_66
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES base cmake pluginterfaces public.sdk)

FetchContent_MakeAvailable(vst3sdk)
