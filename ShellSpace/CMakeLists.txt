cmake_minimum_required(VERSION 3.22)
project(ShellSpace VERSION 0.1.0)

# MSVCは既定でソースをシステムコードページ(日本語環境ならCP932)として読む。
# UTF-8の日本語コメントがShift-JISとして解釈されると、2バイト目が 0x5C(\) の文字が
# 行継続とみなされ「次の行がコメントに飲まれる」。実際にこれで変数宣言が消えた。
# /utf-8 は同時に JUCE 側の C4819 警告も消える。
if(MSVC)
    add_compile_options(/utf-8)

    # CRTを静的リンクする。既定(動的)だと配布先に
    # 「Visual C++ 2015-2022 再頒布可能パッケージ」が無いとロードに失敗し、
    # DAW側は理由を出さずにプラグインを無視するだけなので原因が分からない。
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# JUCE の場所。別の場所に置いた場合は -DJUCE_PATH=... で上書きする
set(JUCE_PATH "C:/Users/user/JUCE" CACHE PATH "Path to the JUCE repository")
add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE)

# 埋め込むIR。先に `py -3 IR/make_ir.py IR` を実行して wav を生成しておくこと
set(IR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../IR")
set(IR_FILES
    "${IR_DIR}/Shell_Kick_body.wav"
    "${IR_DIR}/Shell_Snare_body.wav"
    "${IR_DIR}/Shell_Tom_body.wav"
    "${IR_DIR}/Hall_Yokosuka-type_Full_Stereo.wav"
    "${IR_DIR}/Hall_Yokosuka-type_Drum_Stereo.wav"
    "${IR_DIR}/Hall_Yokosuka-type_Full_TrueStereo.wav"
    "${IR_DIR}/Hall_Yokosuka-type_Drum_TrueStereo.wav")

foreach(f ${IR_FILES})
    if(NOT EXISTS ${f})
        message(FATAL_ERROR
            "IRが見つかりません: ${f}\n"
            "先に `py -3 IR/make_ir.py IR` を実行して wav を生成してください。")
    endif()
endforeach()

juce_add_binary_data(ShellSpaceIRs SOURCES ${IR_FILES})

juce_add_plugin(ShellSpace
    COMPANY_NAME              "Yokosuka"
    PLUGIN_MANUFACTURER_CODE  Ysku
    PLUGIN_CODE               Shsp
    FORMATS                   VST3
    PRODUCT_NAME              "ShellSpace"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    COPY_PLUGIN_AFTER_BUILD   TRUE
    # 既定の C:\Program Files\Common Files\VST3 は管理者権限が要る。
    # ユーザー用VST3フォルダなら権限不要で、CubaseもここをVST3の既定パスとして走査する。
    VST3_COPY_DIR             "$ENV{LOCALAPPDATA}/Programs/Common/VST3")

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
# 検証ホスト: ビルドしたVST3を実際にロードして音を通し、出力を測る。
#   build\ShellSpaceTest_artefacts\Release\ShellSpaceTest.exe <path to .vst3>
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
