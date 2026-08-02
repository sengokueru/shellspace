#pragma once

#include "PluginProcessor.h"

/** ラベル付きノブ1個ぶん。 */
struct Knob  : public juce::Component
{
    Knob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& text);
    void resized() override;

    juce::Slider slider;
    juce::Label  label;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

class ShellSpaceEditor  : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit ShellSpaceEditor (ShellSpaceProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** paint と resized が同じ矩形を見るようにするための共有計算。
        別々に座標を書くとズレる。 */
    juce::Rectangle<int> sectionBounds (int index) const;

private:
    void timerCallback() override;
    void showIRMenu (bool body, juce::Button& source);
    void refreshIRLabels();

    ShellSpaceProcessor& proc;

    juce::ComboBox presetBox;
    juce::ComboBox bodyType, spaceType;

    // 項目を入れてからアタッチする必要があるので、メンバ初期化子では作れない。
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> bodyTypeAtt, spaceTypeAtt;

    juce::TextButton bodyIRButton, spaceIRButton;
    juce::ToggleButton trueStereoToggle { "True Stereo" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> trueStereoAtt;

    juce::Label statusLabel;

    Knob bodyTune, bodyLevel;
    Knob spacePre, spaceLevel;
    Knob hpf, dry, out;

    std::unique_ptr<juce::FileChooser> chooser;
    int lastProgram { -1 };
    juce::String lastStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShellSpaceEditor)
};
