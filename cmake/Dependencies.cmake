# SPDX-License-Identifier: AGPL-3.0-only
# LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#
# Pinned third-party dependencies (plan decision 18). Every FetchContent_Declare name needs a row in
# THIRD_PARTY_LICENSES.md; tools/check-licenses.ps1 enforces this.
include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# JUCE 8 under AGPLv3 (fixed decision; JUCE 9 is out but not adopted).
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.16-security
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)

# Catch2 v3 for unit tests (BSL-1.0).
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.16.1
    GIT_SHALLOW    TRUE)

# Signalsmith Linear (MIT): FFT/STFT used by Signalsmith Stretch. Declared here so the pin is ours, not the
# one inside signalsmith-stretch's CMakeLists (which only declares it when the target does not exist yet).
FetchContent_Declare(signalsmith-linear
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/linear.git
    GIT_TAG        0.6.4
    GIT_SHALLOW    TRUE)

# Signalsmith Stretch (MIT): length change and length-preserving pitch (FR-104–106). Header-only, JUCE-free.
FetchContent_Declare(signalsmith-stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG        1.4.0
    GIT_SHALLOW    TRUE)

# nlohmann/json (MIT): versioned JSON for state and presets (spec §10, FR-003, NFR-012). JUCE-free, used by core/.
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
    GIT_SHALLOW    TRUE)

FetchContent_MakeAvailable(JUCE nlohmann_json signalsmith-linear signalsmith-stretch)

if(KLAY_BUILD_TESTS)
    FetchContent_MakeAvailable(Catch2)
endif()
