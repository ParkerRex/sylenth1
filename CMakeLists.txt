cmake_minimum_required(VERSION 3.24)

if(APPLE AND (NOT DEFINED CMAKE_OSX_ARCHITECTURES OR CMAKE_OSX_ARCHITECTURES STREQUAL ""))
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
endif()

# A fixed deployment floor avoids inheriting the build machine's macOS version.
# macOS 11 is the first native Apple Silicon release; both slices use this floor.
if(APPLE AND (NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL ""))
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum supported macOS version" FORCE)
endif()

project(Synthia
    VERSION 0.1.0
    DESCRIPTION "Synthia lab instrument"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SYNTHIA_ENABLE_TESTS "Build synthia tests" ON)
option(SYNTHIA_ENABLE_COPY_AFTER_BUILD "Copy plugin bundles after build" OFF)
option(SYNTHIA_ENABLE_ASAN "Build with AddressSanitizer enabled" OFF)
option(SYNTHIA_ENABLE_UBSAN "Build with UndefinedBehaviorSanitizer enabled" OFF)
set(SYNTHIA_JUCE_GIT_TAG "8.0.13" CACHE STRING "JUCE git tag used when SYNTHIA_JUCE_PATH is not set")
set(SYNTHIA_JUCE_PATH "" CACHE PATH "Optional local JUCE checkout")

if(SYNTHIA_ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

if(SYNTHIA_ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=undefined)
endif()

if(SYNTHIA_JUCE_PATH)
    add_subdirectory("${SYNTHIA_JUCE_PATH}" juce)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${SYNTHIA_JUCE_GIT_TAG}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(SYNTHIA_COMMON_SOURCES
    src/dsp/Arpeggiator.cpp
    src/dsp/Arpeggiator.h
    src/dsp/Envelope.cpp
    src/dsp/Envelope.h
    src/dsp/Filter.cpp
    src/dsp/Filter.h
    src/dsp/Lfo.cpp
    src/dsp/Lfo.h
    src/dsp/OscillatorStack.cpp
    src/dsp/OscillatorStack.h
    src/dsp/Ramp.cpp
    src/dsp/Ramp.h
    src/dsp/SynthEngine.cpp
    src/dsp/SynthEngine.h
    src/dsp/SynthParameters.h
    src/dsp/fx/FxChain.cpp
    src/dsp/fx/FxChain.h
    src/midi/MidiControllerMap.cpp
    src/midi/MidiControllerMap.h
    src/modulation/ModulationRouteModel.cpp
    src/modulation/ModulationRouteModel.h
    src/plugin/ParameterRegistry.cpp
    src/plugin/ParameterRegistry.h
    src/presets/PresetManager.cpp
    src/presets/PresetManager.h
    src/presets/PresetValidator.cpp
    src/presets/PresetValidator.h
    src/voice/Voice.cpp
    src/voice/Voice.h
    src/voice/VoiceAllocator.cpp
    src/voice/VoiceAllocator.h)

juce_add_plugin(SynthiaPlugin
    COMPANY_NAME "ParkerX"
    BUNDLE_ID "com.parkerx.synthia"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${SYNTHIA_ENABLE_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE PkRx
    PLUGIN_CODE SynA
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Synthia")

target_sources(SynthiaPlugin
    PRIVATE
        ${SYNTHIA_COMMON_SOURCES}
        src/plugin/PluginEditor.cpp
        src/plugin/PluginEditor.h
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginProcessor.h)

foreach(SYNTHIA_PLUGIN_FORMAT_TARGET IN ITEMS SynthiaPlugin_AU SynthiaPlugin_VST3 SynthiaPlugin_Standalone)
    juce_add_bundle_resources_directory(${SYNTHIA_PLUGIN_FORMAT_TARGET} "${CMAKE_CURRENT_SOURCE_DIR}/presets/factory")
endforeach()

target_compile_definitions(SynthiaPlugin
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISABLE_CAUTIOUS_PARAMETER_ID_CHECKING=1
        JUCE_VST3_CAN_REPLACE_VST2=0
        SYNTHIA_PROJECT_VERSION="${PROJECT_VERSION}")

target_link_libraries(SynthiaPlugin
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

juce_generate_juce_header(SynthiaPlugin)

add_executable(SynthiaRender
    ${SYNTHIA_COMMON_SOURCES}
    src/validation/SynthRender.cpp)

target_link_libraries(SynthiaRender
    PRIVATE
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

target_compile_definitions(SynthiaRender
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISABLE_CAUTIOUS_PARAMETER_ID_CHECKING=1
        SYNTHIA_PROJECT_VERSION="${PROJECT_VERSION}")

if(SYNTHIA_ENABLE_TESTS)
    enable_testing()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    add_executable(SynthiaNativeProcessorTest tests/smoke/NativeProcessorTest.cpp)
    target_include_directories(SynthiaNativeProcessorTest
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            $<TARGET_PROPERTY:SynthiaPlugin,JUCE_GENERATED_SOURCES_DIRECTORY>)
    target_link_libraries(SynthiaNativeProcessorTest
        PRIVATE
            SynthiaPlugin
            juce::juce_audio_utils
            juce::juce_dsp)
    add_test(NAME SynthiaNativeProcessorTest COMMAND SynthiaNativeProcessorTest)
    set_tests_properties(SynthiaNativeProcessorTest PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME SynthiaReferenceComparisonTest
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/ReferenceComparisonTest.py")
    set_tests_properties(SynthiaReferenceComparisonTest PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_executable(SynthiaSmokeTest
        ${SYNTHIA_COMMON_SOURCES}
        tests/smoke/SynthSmokeTest.cpp)

    target_link_libraries(SynthiaSmokeTest
        PRIVATE
            juce::juce_core
            juce::juce_audio_basics
            juce::juce_audio_processors
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    target_compile_definitions(SynthiaSmokeTest
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_DISABLE_CAUTIOUS_PARAMETER_ID_CHECKING=1
            SYNTHIA_PROJECT_VERSION="${PROJECT_VERSION}")

    add_test(NAME SynthiaSmokeTest COMMAND SynthiaSmokeTest)
    set_tests_properties(SynthiaSmokeTest PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_executable(SynthiaContractTest
        ${SYNTHIA_COMMON_SOURCES}
        tests/smoke/SynthContractTest.cpp)

    target_link_libraries(SynthiaContractTest
        PRIVATE
            juce::juce_core
            juce::juce_audio_basics
            juce::juce_audio_processors
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    target_compile_definitions(SynthiaContractTest
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_DISABLE_CAUTIOUS_PARAMETER_ID_CHECKING=1
            SYNTHIA_PROJECT_VERSION="${PROJECT_VERSION}")

    add_test(NAME SynthiaContractTest COMMAND SynthiaContractTest)
    set_tests_properties(SynthiaContractTest PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_executable(SynthiaVoiceCoreTest
        ${SYNTHIA_COMMON_SOURCES}
        tests/smoke/SynthVoiceCoreTest.cpp)

    target_link_libraries(SynthiaVoiceCoreTest
        PRIVATE
            juce::juce_core
            juce::juce_audio_basics
            juce::juce_audio_processors
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    target_compile_definitions(SynthiaVoiceCoreTest
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_DISABLE_CAUTIOUS_PARAMETER_ID_CHECKING=1
            SYNTHIA_PROJECT_VERSION="${PROJECT_VERSION}")

    add_test(NAME SynthiaVoiceCoreTest COMMAND SynthiaVoiceCoreTest)
    set_tests_properties(SynthiaVoiceCoreTest PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_executable(SynthiaDspCoreTest
        ${SYNTHIA_COMMON_SOURCES}
        tests/smoke/SynthDspCoreTest.cpp)

    target_link_libraries(SynthiaDspCoreTest
        PRIVATE
            juce::juce_core
            juce::juce_audio_basics
            juce::juce_audio_processors
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    target_compile_definitions(SynthiaDspCoreTest
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_DISABLE_CAUTIOUS_PARAMETER_ID_CHECKING=1
            SYNTHIA_PROJECT_VERSION="${PROJECT_VERSION}")

    add_test(NAME SynthiaDspCoreTest COMMAND SynthiaDspCoreTest)
    set_tests_properties(SynthiaDspCoreTest PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME SynthiaRenderCoreSuite
        COMMAND SynthiaRender --suite core --output-dir "${CMAKE_CURRENT_BINARY_DIR}/reports/ctest-core")
    set_tests_properties(SynthiaRenderCoreSuite PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME SynthiaRandomizeRenderTest
        COMMAND SynthiaRender --randomize-test --output "${CMAKE_CURRENT_BINARY_DIR}/reports/ctest-randomize.json")
    set_tests_properties(SynthiaRandomizeRenderTest PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME SynthiaModulationRouteRenderTest
        COMMAND SynthiaRender --modulation-route-render-test --output "${CMAKE_CURRENT_BINARY_DIR}/reports/ctest-modulation-route-render.json")
    set_tests_properties(SynthiaModulationRouteRenderTest PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME SynthiaOfflineRealtimeCompareTest
        COMMAND SynthiaRender --offline-realtime-compare-test --output "${CMAKE_CURRENT_BINARY_DIR}/reports/ctest-offline-realtime-compare.json")
    set_tests_properties(SynthiaOfflineRealtimeCompareTest PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME SynthiaPatchRecreationSuite
        COMMAND SynthiaRender --suite patch-recreation --output-dir "${CMAKE_CURRENT_BINARY_DIR}/reports/ctest-patch-recreation")
    set_tests_properties(SynthiaPatchRecreationSuite PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
