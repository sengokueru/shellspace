#pragma once

#include "PluginProcessor.h"

/** ラベル付きノブ。音色を決めるパラメータ用。 */
struct Knob  : public juce::Component
{
    Knob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& text);
    void resized() override;

    juce::Slider slider;
    juce::Label  label;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

/** dB目盛り付きの縦フェーダー。レベル系はこちら。
    ミキサーと同じ操作感になるので、量の調整はノブより速い。 */
struct Fader  : public juce::Component,
                private juce::Slider::Listener
{
    Fader (juce::AudioProcessorValueTreeState& s, const juce::String& id,
           const std::vector<float>& scaleMarks);
    ~Fader() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    juce::Slider slider;
    juce::Label  value;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;

private:
    void sliderValueChanged (juce::Slider*) override;
    /** 目盛りのdB値 -> フェーダー内のY座標 */
    float yForDb (float db) const;

    std::vector<float> marks;
    juce::Rectangle<int> trackArea;
};

class ShellSpaceEditor  : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit ShellSpaceEditor (ShellSpaceProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** paint と resized が同じ矩形を見るようにするための共有計算。 */
    juce::Rectangle<int> columnBounds (int index) const;

private:
    void timerCallback() override;
    void showIRMenu (bool body, juce::Button& source);
    void refreshIRLabels();

    ShellSpaceProcessor& proc;

    juce::ComboBox presetBox;
    juce::ComboBox bodyType, spaceType;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> bodyTypeAtt, spaceTypeAtt;

    juce::TextButton bodyIRButton, spaceIRButton;
    juce::ToggleButton trueStereoToggle { "True St." };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> trueStereoAtt;

    // 各チャンネルの MUTE と、マスターの BYPASS
    juce::TextButton muteButtons[4];
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAtts[4];

    juce::Label statusLabel;

    Knob  bodyTune, spacePre, hpf;
    Fader bodyLevel, spaceLevel, dry, out;

    std::unique_ptr<juce::FileChooser> chooser;
    int lastProgram { -1 };
    juce::String lastStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShellSpaceEditor)
};
