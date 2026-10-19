# SPDX-License-Identifier: AGPL-3.0-only
# LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#
# klay_core: platform-neutral model/render/DSP code. Must not link or include JUCE (C-007).
add_library(klay_core STATIC
    model/StateHeader.cpp
    model/StateHeader.h)

target_include_directories(klay_core PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_features(klay_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(klay_core PRIVATE /W4 /permissive- /utf-8)
endif()
