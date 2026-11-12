cmake_minimum_required(VERSION 3.22)
set(VULTUR_VERSION "0.1.0" CACHE STRING "Version embedded in the plugin")
project(Vultur VERSION ${VULTUR_VERSION} LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS")
include(CTest)
option(VULTUR_BUILD_PREVIEW "Build the interactive UI preview" ON)
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG dabd5ad13cd869cba41046947fffc8e5ce5cd662)
FetchContent_MakeAvailable(JUCE)
set(formats VST3 Standalone)
if(APPLE)
    list(APPEND formats AU)
endif()
juce_add_binary_data(VulturAssets SOURCES assets/cuphead.jpg assets/fonts/LuckiestGuy-Regular.ttf)
juce_add_plugin(Vultur
    COMPANY_NAME "Vultur" BUNDLE_ID "audio.vultur.remix"
    PLUGIN_MANUFACTURER_CODE Vltr PLUGIN_CODE Vlt1
    FORMATS ${formats} PRODUCT_NAME "Vultur"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Vultur utiliza la entrada de audio seleccionada para procesar efectos en tiempo real."
    IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Multi)
target_sources(Vultur PRIVATE Source/Effects.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(Vultur PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Vultur PRIVATE VulturAssets juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
if(APPLE)
    foreach(format IN LISTS formats)
        target_sources(Vultur_${format} PRIVATE assets/fonts/LICENSE.txt assets/fonts/NOTICE.txt)
    endforeach()
    set_source_files_properties(assets/fonts/LICENSE.txt assets/fonts/NOTICE.txt PROPERTIES
        HEADER_FILE_ONLY TRUE MACOSX_PACKAGE_LOCATION Resources/licenses)
endif()
if(BUILD_TESTING)
    add_executable(vultur_tests Tests/ProcessorTests.cpp)
    target_include_directories(vultur_tests PRIVATE Source $<TARGET_PROPERTY:Vultur,INCLUDE_DIRECTORIES>)
    target_compile_definitions(vultur_tests PRIVATE $<TARGET_PROPERTY:Vultur,COMPILE_DEFINITIONS>)
    target_link_libraries(vultur_tests PRIVATE Vultur)
    add_test(NAME audio_and_state COMMAND vultur_tests)
    add_executable(vultur_review_tests Tests/ReviewTests.cpp)
    target_include_directories(vultur_review_tests PRIVATE Source $<TARGET_PROPERTY:Vultur,INCLUDE_DIRECTORIES>)
    target_compile_definitions(vultur_review_tests PRIVATE $<TARGET_PROPERTY:Vultur,COMPILE_DEFINITIONS>)
    target_link_libraries(vultur_review_tests PRIVATE Vultur)
    add_test(NAME review_regressions COMMAND vultur_review_tests)
endif()
if(VULTUR_BUILD_PREVIEW)
    add_executable(VulturPreview WIN32 MACOSX_BUNDLE)
    set_target_properties(VulturPreview PROPERTIES OUTPUT_NAME "Vultur Preview"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/VulturPreview_artefacts/$<CONFIG>"
        MACOSX_BUNDLE_GUI_IDENTIFIER "audio.vultur.preview")
    target_sources(VulturPreview PRIVATE Tests/Preview.cpp)
    target_include_directories(VulturPreview PRIVATE Source $<TARGET_PROPERTY:Vultur,INCLUDE_DIRECTORIES>)
    target_compile_definitions(VulturPreview PRIVATE
        "$<FILTER:$<TARGET_PROPERTY:Vultur,COMPILE_DEFINITIONS>,EXCLUDE,^(JUCE_STANDALONE_APPLICATION|JucePlugin_Build_Standalone)=>")
    target_link_libraries(VulturPreview PRIVATE Vultur)
endif()
if(APPLE)
    # Sign complete bundles after JUCE has written resources and the VST3 manifest.
    # This local ad-hoc signature does not publish, notarise or install them.
    add_custom_target(VulturAdHocSign ALL)
    set(sign_targets Vultur_VST3 Vultur_AU Vultur_Standalone)
    if(TARGET VulturPreview)
        list(APPEND sign_targets VulturPreview)
    endif()
    foreach(sign_target IN LISTS sign_targets)
        add_dependencies(VulturAdHocSign ${sign_target})
        add_custom_command(TARGET VulturAdHocSign POST_BUILD
            COMMAND /usr/bin/codesign --force --sign - "$<TARGET_BUNDLE_DIR:${sign_target}>"
            COMMAND /usr/bin/codesign --verify --deep --strict "$<TARGET_BUNDLE_DIR:${sign_target}>"
            VERBATIM)
    endforeach()
endif()
