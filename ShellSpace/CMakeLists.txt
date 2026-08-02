cmake_minimum_required(VERSION 3.22)

# macOS のアーキテクチャと最低対応バージョンは project() より前に決める必要がある。
# Apple Silicon と Intel の両対応（ユニバーサルバイナリ）にする。
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

project(ShellSpace VERSION 0.4.0)

if(MSVC)
    # MSVCは既定でソースをシステムコードページ(日本語環境ならCP932)として読む。
    # UTF-8の日本語コメントがShift-JISとして解釈されると、2バイト目が 0x5C(\) の文字が
    # 行継続とみなされ「次の行がコメントに飲まれる」。実際にこれで変数宣言が消えた。
    add_compile_options(/utf-8)

    # CRTを静的リンクする。既定(動的)だと配布先に
    # 「Visual C++ 2015-2022 再頒布可能パッケージ」が無いとロードに失敗し、
    # DAW側は理由を出さずにプラグインを無視するだけなので原因が分からない。
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

#==============================================================================
# JUCE。ローカルにあればそれを使い、無ければ取得する（CI用）。
set(JUCE_PATH "C:/Users/user/JUCE" CACHE PATH "Path to a local JUCE checkout")

if(EXISTS "${JUCE_PATH}/CMakeLists.txt")
    message(STATUS "Using local JUCE: ${JUCE_PATH}")
    add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE)
else()
    message(STATUS "Local JUCE not found. Fetching...")
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        # 検証したのと同じものを使う（JUCE 9.0.0 相当）
        GIT_TAG        7dda739b9be09f1dfb9592a09e1701acf808cd44
        GIT_SHALLOW    FALSE)
    FetchContent_MakeAvailable(JUCE)
endif()

#==============================================================================
# 埋め込むIR。先に `python IR/make_ir.py IR` を実行して wav を生成しておくこと
set(IR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../IR")
set(IR_FILES
    "${IR_DIR}/Cab_Guitar_4x12.wav"
    "${IR_DIR}/Cab_Bass_8x10.wav"
    "${IR_DIR}/Hall_Yokosuka-type_Full_Stereo.wav"
    "${IR_DIR}/Hall_Yokosuka-type_Drum_Stereo.wav"
    "${IR_DIR}/Hall_Yokosuka-type_Full_TrueStereo.wav"
    "${IR_DIR}/Hall_Yokosuka-type_Drum_TrueStereo.wav")

foreach(kind Kick Snare Tom)
    foreach(material Maple Birch Mahogany Oak)
        foreach(kit Studio Projection Tight Open)
            list(APPEND IR_FILES "${IR_DIR}/Shell_${kind}_${material}_${kit}.wav")
        endforeach()
    endforeach()
endforeach()

foreach(f ${IR_FILES})
    if(NOT EXISTS ${f})
        message(FATAL_ERROR
            "IRが見つかりません: ${f}\n"
            "先に `python IR/make_ir.py IR` を実行して wav を生成してください。")
    endif()
endforeach()

juce_add_binary_data(ShellSpaceIRs SOURCES ${IR_FILES})

#==============================================================================
# プラグイン本体
if(APPLE)
    set(SS_FORMATS VST3 AU)
else()
    set(SS_FORMATS VST3)
endif()

option(SS_COPY_AFTER_BUILD "ビルド後にプラグインフォルダへコピーする" ON)

if(WIN32)
    # 既定の C:\Program Files\Common Files\VST3 は管理者権限が要る。
    # ユーザー用VST3フォルダなら権限不要で、DAWもここを標準パスとして走査する。
    set(SS_VST3_COPY_DIR "$ENV{LOCALAPPDATA}/Programs/Common/VST3")
elseif(APPLE)
    set(SS_VST3_COPY_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3")
else()
    set(SS_VST3_COPY_DIR "$ENV{HOME}/.vst3")
endif()

juce_add_plugin(ShellSpace
    COMPANY_NAME              "Yokosuka"
    PLUGIN_MANUFACTURER_CODE  Ysku
    PLUGIN_CODE               Shsp
    FORMATS                   ${SS_FORMATS}
    PRODUCT_NAME              "ShellSpace"
    BUNDLE_ID                 "com.yokosuka.shellspace"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    COPY_PLUGIN_AFTER_BUILD   ${SS_COPY_AFTER_BUILD}
    VST3_COPY_DIR             "${SS_VST3_COPY_DIR}")

target_sources(ShellSpace PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(ShellSpace PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(ShellSpace PRIVATE
    ShellSpaceIRs
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

#==============================================================================
# DSP検証: プラグインを直接リンクして頑健性を見る。
# VST3ラッパーを通さないので、問題の所在を切り分けられる。
juce_add_console_app(ShellSpaceDspTest PRODUCT_NAME "ShellSpaceDspTest")

target_sources(ShellSpaceDspTest PRIVATE
    Tests/dsp_test.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(ShellSpaceDspTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(ShellSpaceDspTest PRIVATE
    ShellSpaceIRs
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

#==============================================================================
# 検証ホスト: ビルドしたVST3を実際にロードして音を通し、出力を測る。
juce_add_console_app(ShellSpaceTest PRODUCT_NAME "ShellSpaceTest")

target_sources(ShellSpaceTest PRIVATE Tests/host_test.cpp)

target_compile_definitions(ShellSpaceTest PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(ShellSpaceTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

#==============================================================================
# UIスナップショット: エディタを描画してPNGに出す。レイアウトを目で確認するため。
juce_add_console_app(ShellSpaceUI PRODUCT_NAME "ShellSpaceUI")

target_sources(ShellSpaceUI PRIVATE
    Tests/ui_snapshot.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(ShellSpaceUI PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(ShellSpaceUI PRIVATE
    ShellSpaceIRs
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
