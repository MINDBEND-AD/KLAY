# SPDX-License-Identifier: AGPL-3.0-only
# LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#
# klay-render: headless renderer for golden-file tests, listening material and preset QA (spec §10, §12).
juce_add_console_app(klay_render_cli PRODUCT_NAME "klay-render")

target_sources(klay_render_cli PRIVATE
    main.cpp
    Listening.cpp
    Listening.h)

target_compile_definitions(klay_render_cli PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(klay_render_cli
    PRIVATE
        klay_core
        klay_adapters
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    target_compile_options(klay_render_cli PRIVATE /utf-8)
endif()
set_target_properties(klay_render_cli PROPERTIES FOLDER tools)
