#include "PluginEditor.h"

namespace
{
    const juce::Colour kBack   { 0xff1b1d21 };
    const juce::Colour kPanel  { 0xff24272c };
    const juce::Colour kStrip  { 0xff2e333a };
    const juce::Colour kText   { 0xffd8dce3 };
    const juce::Colour kAccent { 0xff6fb2ff };
    const juce::Colour kWarn   { 0xffe0a04a };

    constexpr int kPad      = 12;
    constexpr int kHeader   = 58;   // タイトル + プリセット
    constexpr int kColW     = 100;
    constexpr int kColGap   = 8;
    constexpr int kNumCols  = 4;
    constexpr int kTopH     = 220;  // BODYのType/Material/Kitとノブが収まる高さ
    constexpr int kMuteH    = 26;   // MUTE / BYPASS
    constexpr int kFaderH   = 180;
    constexpr int kNameH    = 24;   // 一番下の名前帯
    constexpr int kStatusH  = 24;
    constexpr int kInner    = 6;
    constexpr int kSmallH   = 20;
    constexpr int kComboH   = 24;

    const juce::Colour kMuteOn { 0xffc94f4f };

    constexpr int kEditorW = kPad * 2 + kColW * kNumCols + kColGap * (kNumCols - 1);
    constexpr int kEditorH = kHeader + kTopH + kMuteH + kFaderH + kNameH + 6 + kStatusH + kPad;

    constexpr int kThumb = 9;   // フェーダーのつまみ半径ぶんの余白

    /** juce::String(const char*) は中身をASCIIとして扱うので、UTF-8の日本語が
        1バイト1文字に分解されて化ける。日本語リテラルは必ずこれを通す。 */
    juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& text)
    : attachment (s, id, slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 66, 15);
    slider.setColour (juce::Slider::rotarySliderFillColourId, kAccent);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxTextColourId, kText);
    addAndMakeVisible (slider);

    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, kText.withAlpha (0.75f));
    label.setFont (juce::FontOptions (10.0f));
    addAndMakeVisible (label);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (13));
    slider.setBounds (r);
}

//==============================================================================
Fader::Fader (juce::AudioProcessorValueTreeState& s, const juce::String& id,
              const std::vector<float>& scaleMarks)
    : attachment (s, id, slider), marks (scaleMarks)
{
    slider.setSliderStyle (juce::Slider::LinearVertical);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setColour (juce::Slider::trackColourId, kAccent.withAlpha (0.55f));
    slider.setColour (juce::Slider::backgroundColourId, kBack);
    slider.setColour (juce::Slider::thumbColourId, kText.withAlpha (0.92f));
    slider.addListener (this);
    addAndMakeVisible (slider);

    value.setJustificationType (juce::Justification::centred);
    value.setColour (juce::Label::textColourId, kText);
    value.setFont (juce::FontOptions (12.0f));
    value.setEditable (false, true, false);  // ダブルクリックでdBを直接入力
    value.setTooltip (u8 ("ダブルクリックして数値入力"));
    value.onTextChange = [this]
    {
        const auto text = value.getText().trim();
        double target = slider.getValue();

        if (text == u8 ("切") || text.equalsIgnoreCase ("off"))
            target = slider.getMinimum();
        else if (text.containsAnyOf ("0123456789"))
            target = text.retainCharacters ("0123456789+-.").getDoubleValue();

        slider.setValue (juce::jlimit (slider.getMinimum(), slider.getMaximum(), target),
                         juce::sendNotificationSync);
        sliderValueChanged (&slider);
    };
    addAndMakeVisible (value);

    sliderValueChanged (&slider);
}

Fader::~Fader()
{
    slider.removeListener (this);
}

void Fader::sliderValueChanged (juce::Slider*)
{
    const double v = slider.getValue();
    value.setText (v <= -59.5 ? u8 ("切") : juce::String (v, 1), juce::dontSendNotification);
}

float Fader::yForDb (float db) const
{
    const auto range = slider.getRange();
    const double t = (db - range.getStart()) / (range.getEnd() - range.getStart());

    const float top    = (float) trackArea.getY() + kThumb;
    const float bottom = (float) trackArea.getBottom() - kThumb;
    return bottom - (float) t * (bottom - top);
}

void Fader::resized()
{
    auto r = getLocalBounds();
    value.setBounds (r.removeFromBottom (16));
    r.removeFromBottom (2);

    trackArea = r.removeFromLeft (44);
    slider.setBounds (trackArea);
}

void Fader::paint (juce::Graphics& g)
{
    // 目盛り。数値だけでなく刻みがあると、量の見当が一目でつく
    auto scale = getLocalBounds().withTrimmedLeft (trackArea.getWidth())
                                 .withTrimmedBottom (18);

    g.setFont (juce::FontOptions (9.0f));

    for (float db : marks)
    {
        const float y = yForDb (db);
        if (y < (float) scale.getY() || y > (float) scale.getBottom())
            continue;

        const bool major = juce::approximatelyEqual (db, 0.0f);

        g.setColour (kText.withAlpha (major ? 0.55f : 0.28f));
        g.drawLine ((float) scale.getX() + 1.0f, y,
                    (float) scale.getX() + (major ? 9.0f : 6.0f), y, major ? 1.4f : 1.0f);

        g.setColour (kText.withAlpha (major ? 0.7f : 0.42f));
        const auto text = db <= -59.5f ? juce::String::charToString ((juce::juce_wchar) 0x221E)
                                       : juce::String ((int) db);
        g.drawText (text, scale.getX() + 12, (int) y - 6, scale.getWidth() - 12, 12,
                    juce::Justification::centredLeft);
    }
}

//==============================================================================
ShellSpaceEditor::ShellSpaceEditor (ShellSpaceProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      bodyTune  (p.apvts, "bodyTune", "TUNE"),
      spacePre  (p.apvts, "spacePre", "PREDELAY"),
      hpf       (p.apvts, "hpf",      "WET HPF"),
      bodyLevel (p.apvts, "bodyLevel",  { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -40.0f, -60.0f }),
      spaceLevel(p.apvts, "spaceLevel", { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -40.0f, -60.0f }),
      dry       (p.apvts, "dry",        {  6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -40.0f, -60.0f }),
      out       (p.apvts, "out",        { 24.0f, 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -24.0f })
{
    auto styleBox = [] (juce::ComboBox& box)
    {
        box.setColour (juce::ComboBox::backgroundColourId, kBack);
        box.setColour (juce::ComboBox::textColourId, kText);
        box.setColour (juce::ComboBox::arrowColourId, kText.withAlpha (0.7f));
        box.setColour (juce::ComboBox::outlineColourId, kText.withAlpha (0.25f));
    };

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
    addAndMakeVisible (bodyMaterial);
    addAndMakeVisible (bodyKit);
    addAndMakeVisible (spaceType);
    setUpChoice (bodyType,     "bodyType",     bodyTypeAtt);
    setUpChoice (bodyMaterial, "bodyMaterial", bodyMaterialAtt);
    setUpChoice (bodyKit,      "bodyKit",      bodyKitAtt);
    setUpChoice (spaceType,    "spaceType",    spaceTypeAtt);

    bodyType.setTooltip (u8 ("ドラム胴鳴り／ギターキャビ／ベースキャビ"));
    bodyMaterial.setTooltip (u8 ("胴材。キャビ選択時は使用しません"));
    bodyKit.setTooltip (u8 ("Yamaha各シリーズの構造・鳴り方を抽象化した合成モデル"));

    for (auto* b : { &bodyIRButton, &spaceIRButton })
    {
        b->setColour (juce::TextButton::buttonColourId, kBack);
        b->setColour (juce::TextButton::textColourOffId, kText.withAlpha (0.8f));
        addAndMakeVisible (b);
    }
    bodyIRButton .onClick = [this] { showIRMenu (true,  bodyIRButton); };
    spaceIRButton.onClick = [this] { showIRMenu (false, spaceIRButton); };

    trueStereoToggle.setColour (juce::ToggleButton::textColourId, kText.withAlpha (0.8f));
    trueStereoToggle.setColour (juce::ToggleButton::tickColourId, kAccent);
    trueStereoToggle.setColour (juce::ToggleButton::tickDisabledColourId, kText.withAlpha (0.4f));
    addAndMakeVisible (trueStereoToggle);
    trueStereoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                        p.apvts, "trueStereo", trueStereoToggle);

    // MUTE / BYPASS。押した状態が赤く残るので、切ってあることが一目で分かる
    {
        const char* ids[]   = { "bodyMute", "spaceMute", "dryMute", "bypass" };
        const char* texts[] = { "MUTE", "MUTE", "MUTE", "BYPASS" };

        for (int i = 0; i < 4; ++i)
        {
            auto& b = muteButtons[i];
            b.setButtonText (texts[i]);
            b.setClickingTogglesState (true);
            b.setColour (juce::TextButton::buttonColourId, kBack);
            b.setColour (juce::TextButton::buttonOnColourId, kMuteOn);
            b.setColour (juce::TextButton::textColourOffId, kText.withAlpha (0.65f));
            b.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
            addAndMakeVisible (b);

            muteAtts[i] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                              p.apvts, ids[i], b);
        }
    }

    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setFont (juce::FontOptions (10.5f));
    statusLabel.setColour (juce::Label::textColourId, kText.withAlpha (0.55f));
    addAndMakeVisible (statusLabel);

    for (auto* k : { &bodyTune, &spacePre, &hpf })
        addAndMakeVisible (k);
    for (auto* f : { &bodyLevel, &spaceLevel, &dry, &out })
        addAndMakeVisible (f);

    refreshIRLabels();
    startTimerHz (4);

    setSize (kEditorW, kEditorH);
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
        if (name.length() > 8)
            name = name.substring (0, 7) + juce::String::charToString ((juce::juce_wchar) 0x2026);

        return "IR: " + name;
    };

    bodyIRButton .setButtonText (text (true));
    spaceIRButton.setButtonText (text (false));
}

void ShellSpaceEditor::timerCallback()
{
    const int program = proc.getCurrentProgram();
    if (program != lastProgram)
    {
        lastProgram = program;
        presetBox.setSelectedId (program + 1, juce::dontSendNotification);
        refreshIRLabels();
    }

    const int bodyTypeIndex = (int) proc.apvts.getRawParameterValue ("bodyType")->load();
    if (bodyTypeIndex != lastBodyType)
    {
        lastBodyType = bodyTypeIndex;
        const bool drum = bodyTypeIndex < 3;
        bodyMaterial.setEnabled (drum);
        bodyKit.setEnabled (drum);
    }

    const auto bodyErr  = proc.getIRError (true);
    const auto spaceErr = proc.getIRError (false);

    juce::String status;
    if (bodyErr .isNotEmpty())      status = "BODY: "  + bodyErr;
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
juce::Rectangle<int> ShellSpaceEditor::columnBounds (int index) const
{
    return { kPad + index * (kColW + kColGap),
             kHeader,
             kColW,
             kTopH + kMuteH + kFaderH + kNameH };
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

    const char* names[] = { "BODY", "SPACE", "DRY", "MASTER" };

    for (int i = 0; i < kNumCols; ++i)
    {
        auto col = columnBounds (i);

        g.setColour (kPanel);
        g.fillRoundedRectangle (col.toFloat(), 5.0f);

        // 一番下の名前帯。チャンネルストリップの見出し
        auto strip = col.removeFromBottom (kNameH);
        juce::Path p;
        p.addRoundedRectangle ((float) strip.getX(), (float) strip.getY(),
                               (float) strip.getWidth(), (float) strip.getHeight(),
                               5.0f, 5.0f, false, false, true, true);
        g.setColour (kStrip);
        g.fillPath (p);

        g.setColour (i == kNumCols - 1 ? kAccent : kText.withAlpha (0.8f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (names[i], strip, juce::Justification::centred);
    }
}

void ShellSpaceEditor::resized()
{
    presetBox.setBounds (kPad, 26, getWidth() - kPad * 2, kComboH);

    auto columnTop = [this] (int index)
    {
        return columnBounds (index).withHeight (kTopH).reduced (kInner, 0)
                                   .withTrimmedTop (8);
    };

    // 行の高さを固定して、どの列でもノブが同じ大きさになるようにする。
    // 使わない行も場所だけ空けておかないと、行数の少ない列のノブだけ巨大になる。
    auto layoutColumn = [this, &columnTop] (int index, juce::ComboBox* box1,
                                            juce::ComboBox* box2, juce::ComboBox* box3,
                                            juce::Button* irButton, juce::Button* toggle,
                                            Knob& knob)
    {
        auto r = columnTop (index);

        auto row = [&r] (int h, int gap) { auto a = r.removeFromTop (h); r.removeFromTop (gap); return a; };

        auto comboArea1 = row (kComboH, 4);
        auto comboArea2 = row (kComboH, 4);
        auto comboArea3 = row (kComboH, 4);
        auto irArea     = row (kSmallH, 2);
        auto toggleArea = row (kSmallH, 4);

        if (box1      != nullptr) box1     ->setBounds (comboArea1);
        if (box2      != nullptr) box2     ->setBounds (comboArea2);
        if (box3      != nullptr) box3     ->setBounds (comboArea3);
        if (irButton  != nullptr) irButton ->setBounds (irArea);
        if (toggle    != nullptr) toggle   ->setBounds (toggleArea);

        knob.setBounds (r);
    };

    layoutColumn (0, &bodyType,  &bodyMaterial, &bodyKit,
                  &bodyIRButton, nullptr, bodyTune);
    layoutColumn (1, &spaceType, nullptr, nullptr,
                  &spaceIRButton, &trueStereoToggle, spacePre);
    layoutColumn (3, nullptr, nullptr, nullptr,
                  nullptr, nullptr, hpf);

    // ---- MUTE / BYPASS とフェーダー -----------------------------------------
    Fader* faders[] = { &bodyLevel, &spaceLevel, &dry, &out };
    for (int i = 0; i < kNumCols; ++i)
    {
        auto col = columnBounds (i);
        col.removeFromBottom (kNameH);
        col.removeFromTop (kTopH);

        auto mute = col.removeFromTop (kMuteH).reduced (kInner, 3);
        muteButtons[i].setBounds (mute);

        faders[i]->setBounds (col.reduced (kInner, 4));
    }

    statusLabel.setBounds (kPad, getHeight() - kStatusH - 2, getWidth() - kPad * 2, kStatusH);
}
