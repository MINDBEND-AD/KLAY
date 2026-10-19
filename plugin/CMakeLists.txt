# SPDX-License-Identifier: AGPL-3.0-only
# LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).

# Plugin identity is fixed from M0 and never changes (plan decision 4, spec §6).
juce_add_plugin(KLAY
    PRODUCT_NAME                "Klaymore"
    COMPANY_NAME                "MINDBEND AD"
    BUNDLE_ID                   "com.01.klay"
    PLUGIN_MANUFACTURER_CODE    KLAY
    PLUGIN_CODE                 klay
    VERSION                     ${PROJECT_VERSION}
    FORMATS                     VST3
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Instrument Drum
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(KLAY PRIVATE
    PluginProcessor.cpp
    PluginProcessor.h
    PluginEditor.cpp
    PluginEditor.h)

target_compile_definitions(KLAY
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(KLAY
    PRIVATE
        klay_core
        klay_adapters
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    target_compile_options(KLAY PRIVATE /utf-8)
endif()
