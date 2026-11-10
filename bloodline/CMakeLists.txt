cmake_minimum_required(VERSION 3.22)
project(Bloodline VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BLOODLINE_BUILD_TESTS   "Build offline DSP tests (no JUCE needed)" ON)
option(BLOODLINE_BUILD_PLUGINS "Build the VST3 plugins (fetches JUCE)"   ON)

if(BLOODLINE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(Ghost/tests)
endif()

if(BLOODLINE_BUILD_PLUGINS)
  include(FetchContent)
  # Pin to whatever JUCE 8 tag your SonyTune / Crush+Lock builds already use.
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    ON)
  FetchContent_MakeAvailable(JUCE)
  add_subdirectory(Ghost)
endif()
