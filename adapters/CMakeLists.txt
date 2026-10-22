# SPDX-License-Identifier: AGPL-3.0-only
# LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#
# klay_adapters: JUCE implementations of core interfaces (C-007): sample loading and analysis, resampling. An INTERFACE
# library, so each consumer (the plugin, tools/render-cli) compiles the sources against its own JUCE modules.
add_library(klay_adapters INTERFACE)
target_sources(klay_adapters INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/JuceResampler.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SampleAnalyzer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SampleAnalyzer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SampleLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SampleLoader.h)
target_include_directories(klay_adapters INTERFACE ${PROJECT_SOURCE_DIR})
