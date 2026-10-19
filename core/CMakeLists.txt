# SPDX-License-Identifier: AGPL-3.0-only
# LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#
# klay_core: platform-neutral model/render/DSP code. Must not link or include JUCE (C-007).
add_library(klay_core STATIC
    model/Kick.cpp
    model/Kick.h
    model/KickJson.cpp
    model/KickJson.h
    dsp/Fft.cpp
    dsp/Fft.h
    render/Envelope.cpp
    render/Envelope.h
    render/Resampler.cpp
    render/Resampler.h
    render/StereoBuffer.h
    render/Stretcher.cpp
    render/Stretcher.h
    render/TimeBase.cpp
    render/TimeBase.h
    rt/RtGuard.h
    rt/RtMutex.h)

target_include_directories(klay_core PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_features(klay_core PUBLIC cxx_std_20)
target_link_libraries(klay_core PRIVATE nlohmann_json::nlohmann_json signalsmith-stretch)

if(MSVC)
    target_compile_options(klay_core PRIVATE /W4 /permissive- /utf-8 /bigobj /external:anglebrackets /external:W0)
endif()
