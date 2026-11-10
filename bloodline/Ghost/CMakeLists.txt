juce_add_plugin(Ghost
  COMPANY_NAME "CrushAudio"
  PLUGIN_MANUFACTURER_CODE Crsh
  PLUGIN_CODE Ghst
  FORMATS VST3
  PRODUCT_NAME "GHOST"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Fx)

target_sources(Ghost PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp)

target_include_directories(Ghost PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Source
  ${CMAKE_CURRENT_SOURCE_DIR}/../Kit/include)

target_compile_definitions(Ghost PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Ghost
  PRIVATE juce::juce_audio_utils
  PUBLIC  juce::juce_recommended_config_flags
          juce::juce_recommended_lto_flags
          juce::juce_recommended_warning_flags)
