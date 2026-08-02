#include "PluginEditor.h"

namespace
{
    const juce::Colour kBack   { 0xff1b1d21 };
    const juce::Colour kPanel  { 0xff24272c };
    const juce::Colour kText   { 0xffd8dce3 };
    const juce::Colour kAccent { 0xff6fb2ff };
    const juce::Colour kWarn   { 0xffe0a04a };

    constexpr int kPad       = 12;   // 外周
    constexpr int kHeader    = 58;   // タイトル + プリセット
    constexpr int kTitleH    = 22;   // セクション見出し
    constexpr int kKnobW     = 78;
    constexpr int kKnobH     = 88;
    constexpr int kKnobGap   = 4;
    constexpr int kComboW    = 108;  // "Hall Drum" が省略されない幅
    constexpr int kComboH    = 26;
    constexpr int kSmallH    = 20;
    constexpr int kInnerPad  = 10;   // セクション内側
    constexpr int kGap       = 10;   // セクション間
    constexpr int kSectionH  = kTitleH + kKnobH + 8;
    constexpr int kStatusH   = 26;

    /** juce::String(const char*) は中身をASCIIとして扱うので、UTF-8の日本語が
        1バイト1文字に分解されて化ける。日本語リテラルは必ずこれを通す。 */
    juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& text)
    : attachment (s, id, slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    slider.setColour (juce::Slider::rotarySliderFillColourId, kAccent);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxTextColourId, kText);
    addAndMakeVisible (slider);

    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, kText.withAlpha (0.75f));
    label.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (label);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (14));
    slider.setBounds (r);
}

//==============================================================================
ShellSpaceEditor::ShellSpaceEditor (ShellSpaceProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      bodyTune  (p.apvts, "bodyTune",   "TUNE"),
      bodyLevel (p.apvts, "bodyLevel",  "LEVEL"),
      spacePre  (p.apvts, "spacePre",   "PREDELAY"),
      spaceLevel(p.apvts, "spaceLevel", "LEVEL"),
      hpf       (p.apvts, "hpf",        "WET HPF"),
      dry       (p.apvts, "dry",        "DRY"),
      out       (p.apvts, "out",        "OUTPUT")
{
    auto styleBox = [] (juce::ComboBox& box)
    {
        box.setColour (juce::ComboBox::backgroundColourId, kBack);
        box.setColour (juce::ComboBox::textColourId, kText);
        box.setColour (juce::ComboBox::arrowColourId, kText.withAlpha (0.7f));
        box.setColour (juce::ComboBox::outlineColourId, kText.withAlpha (0.25f));
    };

    // ---- プリセット --------------------------------------------------------
    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem (proc.getProgramName (i), i + 1);

    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this]
    {
        const int index = presetBox.getSelectedId() - 1;
        if (index >= 0 && index != proc.getCurrentProgram())
            proc.setCurrentProgram (index);
    };
    styleBox (presetBox);
    addAndMakeVisible (presetBox);

    // ---- 音源/ホール選択 ---------------------------------------------------
    // ComboBoxAttachment は項目を作ってくれない。先に入れてからアタッチする。
    auto setUpChoice = [&p, &styleBox] (juce::ComboBox& box, const juce::String& paramID,
                                        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>& att)
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (paramID)))
            box.addItemList (choice->choices, 1);

        styleBox (box);
        att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                  p.apvts, paramID, box);
    };

    addAndMakeVisible (bodyType);
    addAndMakeVisible (spaceType);
    setUpChoice (bodyType,  "bodyType",  bodyTypeAtt);
    setUpChoice (spaceType, "spaceType", spaceTypeAtt);

    // ---- IR差し替え --------------------------------------------------------
    for (auto* b : { &bodyIRButton, &spaceIRButton })
    {
        b->setColour (juce::TextButton::buttonColourId, kBack);
        b->setColour (juce::TextButton::textColourOffId, kText.withAlpha (0.8f));
        addAndMakeVisible (b);
    }
    bodyIRButton .onClick = [this] { showIRMenu (true,  bodyIRButton); };
    spaceIRButton.onClick = [this] { showIRMenu (false, spaceIRButton); };

    // ---- True Stereo -------------------------------------------------------
    trueStereoToggle.setColour (juce::ToggleButton::textColourId, kText.withAlpha (0.8f));
    trueStereoToggle.setColour (juce::ToggleButton::tickColourId, kAccent);
    trueStereoToggle.setColour (juce::ToggleButton::tickDisabledColourId, kText.withAlpha (0.4f));
    addAndMakeVisible (trueStereoToggle);
    trueStereoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                        p.apvts, "trueStereo", trueStereoToggle);

    // ---- 状態表示 ----------------------------------------------------------
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setFont (juce::FontOptions (10.5f));
    statusLabel.setColour (juce::Label::textColourId, kText.withAlpha (0.55f));
    addAndMakeVisible (statusLabel);

    for (auto* k : { &bodyTune, &bodyLevel, &spacePre, &spaceLevel, &hpf, &dry, &out })
        addAndMakeVisible (k);

    refreshIRLabels();
    startTimerHz (4);

    setSize (ShellSpaceProcessor::kEditorWidth, ShellSpaceProcessor::kEditorHeight);
}

//==============================================================================
void ShellSpaceEditor::showIRMenu (bool body, juce::Button& source)
{
    juce::PopupMenu menu;
    menu.addItem (1, u8 ("wavファイルを読み込む..."));
    menu.addItem (2, u8 ("内蔵IRに戻す"), proc.getUserIR (body) != juce::File());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (source),
                        [this, body] (int result)
    {
        if (result == 2)
        {
            proc.setUserIR (body, juce::File());
            refreshIRLabels();
        }
        else if (result == 1)
        {
            chooser = std::make_unique<juce::FileChooser> (
                u8 (body ? "胴鳴りIRを選ぶ" : "ホールIRを選ぶ（True Stereoは4ch）"),
                juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                "*.wav;*.aiff;*.aif;*.flac");

            chooser->launchAsync (juce::FileBrowserComponent::openMode
                                    | juce::FileBrowserComponent::canSelectFiles,
                                  [this, body] (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (file.existsAsFile())
                    proc.setUserIR (body, file);

                refreshIRLabels();
            });
        }
    });
}

void ShellSpaceEditor::refreshIRLabels()
{
    auto text = [this] (bool body) -> juce::String
    {
        const auto f = proc.getUserIR (body);
        if (f == juce::File())
            return u8 ("IR: 内蔵");

        auto name = f.getFileNameWithoutExtension();
        if (name.length() > 12)
            name = name.substring (0, 11) + juce::String::charToString ((juce::juce_wchar) 0x2026);

        return "IR: " + name;
    };

    bodyIRButton .setButtonText (text (true));
    spaceIRButton.setButtonText (text (false));
}

void ShellSpaceEditor::timerCallback()
{
    // プリセットはホスト側からも変わるので追随させる
    const int program = proc.getCurrentProgram();
    if (program != lastProgram)
    {
        lastProgram = program;
        presetBox.setSelectedId (program + 1, juce::dontSendNotification);
        refreshIRLabels();
    }

    // IRの読み込み結果を出す。黙って内蔵に戻ると原因が分からないため。
    const auto bodyErr  = proc.getIRError (true);
    const auto spaceErr = proc.getIRError (false);

    juce::String status;
    if (bodyErr .isNotEmpty()) status = "BODY: "  + bodyErr;
    else if (spaceErr.isNotEmpty()) status = "SPACE: " + spaceErr;

    if (status != lastStatus)
    {
        lastStatus = status;
        statusLabel.setText (status, juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId,
                               status.isEmpty() ? kText.withAlpha (0.55f) : kWarn);
        repaint();
    }
}

//==============================================================================
juce::Rectangle<int> ShellSpaceEditor::sectionBounds (int index) const
{
    return { kPad,
             kHeader + index * (kSectionH + kGap),
             getWidth() - kPad * 2,
             kSectionH };
}

void ShellSpaceEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBack);

    g.setColour (kText);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ("ShellSpace", kPad, 4, 200, 18, juce::Justification::left);

    g.setColour (kText.withAlpha (0.4f));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("convolution / body + hall", kPad, 6, getWidth() - kPad * 2, 14,
                juce::Justification::right);

    const char* titles[] = { "BODY", "SPACE", "OUTPUT" };

    for (int i = 0; i < 3; ++i)
    {
        const auto area = sectionBounds (i);

        g.setColour (kPanel);
        g.fillRoundedRectangle (area.toFloat(), 6.0f);

        g.setColour (kAccent.withAlpha (0.85f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (titles[i], area.getX() + kInnerPad, area.getY() + 6, 140, 12,
                    juce::Justification::left);
    }
}

void ShellSpaceEditor::resized()
{
    presetBox.setBounds (kPad, 26, getWidth() - kPad * 2, kComboH);

    // セクション0(BODY) と 1(SPACE): [選択 + IRボタン(+True Stereo)] [ノブ] [ノブ]
    auto layoutWithChoice = [this] (int index, juce::ComboBox& box, juce::Button& irButton,
                                    juce::Button* toggle, Knob& a, Knob& b)
    {
        auto row = sectionBounds (index).reduced (kInnerPad, 0);
        row.removeFromTop (kTitleH);
        row = row.withHeight (kKnobH);

        auto left = row.removeFromLeft (kComboW);
        const int stackH = kComboH + 4 + kSmallH + (toggle != nullptr ? 4 + kSmallH : 0);
        left = left.withSizeKeepingCentre (kComboW, stackH);

        box.setBounds (left.removeFromTop (kComboH));
        left.removeFromTop (4);
        irButton.setBounds (left.removeFromTop (kSmallH));

        if (toggle != nullptr)
        {
            left.removeFromTop (4);
            toggle->setBounds (left.removeFromTop (kSmallH));
        }

        row.removeFromLeft (kGap);
        a.setBounds (row.removeFromLeft (kKnobW));
        row.removeFromLeft (kKnobGap);
        b.setBounds (row.removeFromLeft (kKnobW));
    };

    layoutWithChoice (0, bodyType,  bodyIRButton,  nullptr,           bodyTune, bodyLevel);
    layoutWithChoice (1, spaceType, spaceIRButton, &trueStereoToggle, spacePre, spaceLevel);

    // セクション2(OUTPUT): ノブ3個を横並びで中央寄せ
    auto row = sectionBounds (2).reduced (kInnerPad, 0);
    row.removeFromTop (kTitleH);
    row = row.withHeight (kKnobH);
    row = row.withSizeKeepingCentre (kKnobW * 3 + kKnobGap * 2, kKnobH);

    for (auto* k : { &hpf, &dry, &out })
    {
        k->setBounds (row.removeFromLeft (kKnobW));
        row.removeFromLeft (kKnobGap);
    }

    statusLabel.setBounds (kPad, getHeight() - kStatusH, getWidth() - kPad * 2, kStatusH - 6);
}
