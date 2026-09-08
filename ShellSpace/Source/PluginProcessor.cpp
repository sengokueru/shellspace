#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

#include <cmath>

namespace
{
    const char* kBodyKinds[]    = { "Kick", "Snare", "Tom" };
    const char* kBodyMaterials[] = { "Maple", "Birch", "Mahogany", "Oak" };
    const char* kBodyKits[]      = { "Studio", "Projection", "Tight", "Open" };

    juce::String builtInBodyFile (int type, int material, int kit)
    {
        if (type == 3) return "Cab_Guitar_4x12.wav";
        if (type == 4) return "Cab_Bass_8x10.wav";

        return "Shell_" + juce::String (kBodyKinds[juce::jlimit (0, 2, type)])
             + "_" + kBodyMaterials[juce::jlimit (0, 3, material)]
             + "_" + kBodyKits[juce::jlimit (0, 3, kit)] + ".wav";
    }

    const char* kSpaceFiles[] = { "Hall_Yokosuka-type_Full_Stereo.wav",
                                  "Hall_Yokosuka-type_Drum_Stereo.wav" };

    const char* kSpaceFiles4ch[] = { "Hall_Yokosuka-type_Full_TrueStereo.wav",
                                     "Hall_Yokosuka-type_Drum_TrueStereo.wav" };

    /** 埋め込みリソースを「元のファイル名」で引く。
        juce_add_binary_data が付ける識別子の綴りに依存しないようにするため。 */
    const char* findResource (const juce::String& originalFileName, int& sizeOut)
    {
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
            if (originalFileName == juce::String (BinaryData::originalFilenames[i]))
                return BinaryData::getNamedResource (BinaryData::namedResourceList[i], sizeOut);

        sizeOut = 0;
        return nullptr;
    }

    float dbToGain (float db) { return db <= -59.5f ? 0.0f : juce::Decibels::decibelsToGain (db); }

    /** juce::String(const char*) は中身をASCIIとして扱うので、UTF-8の日本語が
        1バイト1文字に分解されて化ける。日本語リテラルは必ずこれを通す。 */
    juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

    constexpr const char* kBodyIRProperty  = "userBodyIR";
    constexpr const char* kSpaceIRProperty = "userSpaceIR";

    /** 4chバッファから 2ch ぶんを切り出す */
    juce::AudioBuffer<float> extractPair (const juce::AudioBuffer<float>& src, int firstChannel)
    {
        juce::AudioBuffer<float> out (2, src.getNumSamples());
        for (int ch = 0; ch < 2; ++ch)
        {
            const int s = firstChannel + ch;
            if (s < src.getNumChannels())
                out.copyFrom (ch, 0, src, s, 0, src.getNumSamples());
            else if (src.getNumChannels() == 1)
                out.copyFrom (ch, 0, src, 0, 0, src.getNumSamples());
            else
                out.clear (ch, 0, src.getNumSamples());
        }
        return out;
    }

    //==============================================================================
    struct Preset
    {
        const char* name;
        float bodyType, bodyMaterial, bodyKit, bodyTune, bodyLevel;
        float spaceType, spacePre, spaceLevel, trueStereo;
        float hpf, dry, out;
    };

    // 値は正規化(0..1)。dB系は (値+60)/72、dryは (値+60)/66。
    const Preset kPresets[] =
    {
        // name                type mat    kit   tune level  space pre    level  TS    hpf   dry    out
        { "Init",              0.0f, 0.333f, 0.0f, 0.5f, 0.000f, 1.0f, 0.167f, 0.000f, 0.0f, 0.0f, 0.909f, 0.5f },
        { "Kick Body",         0.0f, 0.333f, 0.0f, 0.5f, 0.556f, 1.0f, 0.167f, 0.000f, 0.0f, 0.0f, 0.909f, 0.5f },
        { "Snare Body",       0.25f, 0.333f, 0.0f, 0.5f, 0.556f, 1.0f, 0.167f, 0.000f, 0.0f, 0.0f, 0.909f, 0.5f },
        { "Tom Body",         0.50f, 0.333f, 0.0f, 0.5f, 0.583f, 1.0f, 0.167f, 0.000f, 0.0f, 0.0f, 0.909f, 0.5f },
        // キャビは原音を「置き換える」もの。Dryを混ぜるとキャビの意味が消えるので切る。
        { "Guitar Cab",       0.75f, 0.333f, 0.0f, 0.5f, 0.833f, 1.0f, 0.167f, 0.000f, 0.0f, 0.0f, 0.000f, 0.5f },
        { "Bass Cab",         1.00f, 0.333f, 0.0f, 0.5f, 0.833f, 1.0f, 0.167f, 0.000f, 0.0f, 0.0f, 0.000f, 0.5f },
        { "Drum Hall (Send)",  0.0f, 0.333f, 0.0f, 0.5f, 0.000f, 1.0f, 0.250f, 0.833f, 1.0f, 0.35f, 0.000f, 0.5f },
        { "Full Hall (Send)",  0.0f, 0.333f, 0.0f, 0.5f, 0.000f, 0.0f, 0.167f, 0.833f, 1.0f, 0.0f, 0.000f, 0.5f },
        { "Body + Hall",       0.0f, 0.333f, 0.0f, 0.5f, 0.500f, 1.0f, 0.250f, 0.472f, 1.0f, 0.30f, 0.909f, 0.5f },
    };

    constexpr int kNumPresets = (int) (sizeof (kPresets) / sizeof (Preset));
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ShellSpaceProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "bodyType", 1 }, "Body Type",
        StringArray { "Kick", "Snare", "Tom", "Guitar 4x12", "Bass 8x10" }, 0));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "bodyMaterial", 1 }, "Shell Material",
        StringArray { "Maple", "Birch", "Mahogany", "Oak" }, 1));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "bodyKit", 1 }, "Shell Character",
        StringArray { "Studio", "Projection", "Tight", "Open" }, 0));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "bodyTune", 1 }, "Body Tune",
        NormalisableRange<float> { -12.0f, 12.0f, 0.01f }, 0.0f,
        AudioParameterFloatAttributes{}.withLabel ("st")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "bodyLevel", 1 }, "Body Level",
        NormalisableRange<float> { -60.0f, 12.0f, 0.1f }, -60.0f,
        AudioParameterFloatAttributes{}.withLabel ("dB")));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "spaceType", 1 }, "Space Type",
        StringArray { "Hall Full", "Hall Drum" }, 1));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "spacePre", 1 }, "Predelay",
        NormalisableRange<float> { 0.0f, 120.0f, 0.1f }, 20.0f,
        AudioParameterFloatAttributes{}.withLabel ("ms")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "spaceLevel", 1 }, "Space Level",
        NormalisableRange<float> { -60.0f, 12.0f, 0.1f }, -60.0f,
        AudioParameterFloatAttributes{}.withLabel ("dB")));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "trueStereo", 1 }, "True Stereo", false));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "bodyMute", 1 }, "Body Mute", false));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "spaceMute", 1 }, "Space Mute", false));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "dryMute", 1 }, "Dry Mute", false));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "bypass", 1 }, "Bypass", false));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "hpf", 1 }, "Wet HPF",
        NormalisableRange<float> { 20.0f, 400.0f, 1.0f, 0.4f }, 20.0f,
        AudioParameterFloatAttributes{}.withLabel ("Hz")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "dry", 1 }, "Dry",
        NormalisableRange<float> { -60.0f, 6.0f, 0.1f }, 0.0f,
        AudioParameterFloatAttributes{}.withLabel ("dB")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "out", 1 }, "Output",
        NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f,
        AudioParameterFloatAttributes{}.withLabel ("dB")));

    return layout;
}

//==============================================================================
ShellSpaceProcessor::ShellSpaceProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    formatManager.registerBasicFormats();

    for (auto* id : { "bodyType", "bodyMaterial", "bodyKit", "bodyTune", "spaceType", "trueStereo" })
        apvts.addParameterListener (id, this);
}

ShellSpaceProcessor::~ShellSpaceProcessor()
{
    stopTimer();
    cancelPendingUpdate();

    for (auto* id : { "bodyType", "bodyMaterial", "bodyKit", "bodyTune", "spaceType", "trueStereo" })
        apvts.removeParameterListener (id, this);
}

void ShellSpaceProcessor::parameterChanged (const juce::String& id, float)
{
    if (id == "bodyType" || id == "bodyMaterial" || id == "bodyKit" || id == "bodyTune")
        bodyDirty = true;
    if (id == "spaceType" || id == "trueStereo") spaceDirty = true;
    triggerAsyncUpdate();
}

void ShellSpaceProcessor::handleAsyncUpdate()
{
    // Throttle rather than debounce: continuous automation must not postpone
    // every pending update forever.
    if (! isTimerRunning()) startTimer (kIRReloadDelayMs);
}

void ShellSpaceProcessor::timerCallback()
{
    stopTimer();

    if (bodyDirty.exchange (false))  reloadBodyIR();
    if (spaceDirty.exchange (false)) reloadSpaceIR();
}

//==============================================================================
// ファクトリープリセット
int ShellSpaceProcessor::getNumPrograms() { return kNumPresets; }

const juce::String ShellSpaceProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, kNumPresets) ? kPresets[index].name : juce::String();
}

void ShellSpaceProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, kNumPresets))
        return;

    currentProgram = index;
    const auto& p = kPresets[index];

    struct { const char* id; float v; } values[] = {
        { "bodyType",   p.bodyType   }, { "bodyMaterial", p.bodyMaterial },
        { "bodyKit",    p.bodyKit    }, { "bodyTune",  p.bodyTune  },
        { "bodyLevel",  p.bodyLevel  }, { "spaceType", p.spaceType },
        { "spacePre",   p.spacePre   }, { "spaceLevel", p.spaceLevel },
        { "trueStereo", p.trueStereo }, { "hpf",       p.hpf       },
        { "dry",        p.dry        }, { "out",       p.out       },
    };

    for (auto& v : values)
        if (auto* param = apvts.getParameter (v.id))
            param->setValueNotifyingHost (v.v);

    // ミュート類はプリセットに含めない。切り替えたら必ず解除しておく
    // (残っていると「プリセットを選んだのに鳴らない」になる)
    for (auto* id : { "bodyMute", "spaceMute", "dryMute", "bypass" })
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (0.0f);
}

//==============================================================================
// ユーザーIR
void ShellSpaceProcessor::setUserIR (bool body, const juce::File& file)
{
    const juce::String path = file.existsAsFile() ? file.getFullPathName() : juce::String();

    apvts.state.setProperty (body ? kBodyIRProperty : kSpaceIRProperty, path, nullptr);

    {
        const juce::ScopedLock sl (stateLock);
        (body ? userBodyIRPath : userSpaceIRPath) = path;
    }

    if (body) bodyDirty = true;
    else      spaceDirty = true;

    triggerAsyncUpdate();
}

void ShellSpaceProcessor::refreshUserIRPaths()
{
    const auto b = apvts.state.getProperty (kBodyIRProperty).toString();
    const auto s = apvts.state.getProperty (kSpaceIRProperty).toString();

    const juce::ScopedLock sl (stateLock);
    userBodyIRPath  = b;
    userSpaceIRPath = s;
}

juce::File ShellSpaceProcessor::getUserIR (bool body) const
{
    juce::String path;
    {
        const juce::ScopedLock sl (stateLock);
        path = body ? userBodyIRPath : userSpaceIRPath;
    }
    return path.isEmpty() ? juce::File() : juce::File (path);
}

juce::String ShellSpaceProcessor::getIRError (bool body) const
{
    const juce::ScopedLock sl (stateLock);
    return body ? bodyIRError : spaceIRError;
}

juce::AudioBuffer<float> ShellSpaceProcessor::readIR (const juce::File& userFile,
                                                      const juce::String& builtInName,
                                                      double& sampleRateOut,
                                                      juce::String& errorOut)
{
    errorOut = {};
    sampleRateOut = 48000.0;
    auto decode = [&sampleRateOut, &errorOut] (juce::AudioFormatReader* reader) -> juce::AudioBuffer<float>
    {
        if (reader == nullptr)
        {
            errorOut = u8 ("読めないIR形式（内蔵IRを使用）");
            return {};
        }
        const auto rate = reader->sampleRate;
        const auto length = reader->lengthInSamples;
        const auto channels = reader->numChannels;
        if (! std::isfinite (rate) || rate < 8000.0 || rate > 384000.0
            || length <= 0 || length > rate * 30.0
            || (channels != 1 && channels != 2 && channels != 4))
        {
            errorOut = u8 ("IRは30秒以内・1/2/4ch・8〜384kHzが必要です（内蔵IRを使用）");
            return {};
        }
        juce::AudioBuffer<float> buf ((int) channels, (int) length);
        buf.clear();
        if (! reader->read (&buf, 0, (int) length, 0, true, true))
        {
            errorOut = u8 ("IRの読み取り失敗（内蔵IRを使用）");
            return {};
        }
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int i = 0; i < buf.getNumSamples(); ++i)
                if (! std::isfinite (buf.getSample (ch, i)))
                {
                    errorOut = u8 ("IRにNaN/Infが含まれています（内蔵IRを使用）");
                    return {};
                }
        sampleRateOut = rate;
        return buf;
    };

    if (userFile != juce::File())
    {
        if (userFile.existsAsFile())
        {
            std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (userFile));
            auto result = decode (reader.get());
            if (result.getNumSamples() > 0) return result;
        }
        else errorOut = u8 ("IRが見つかりません（内蔵IRを使用）: ") + userFile.getFileName();
    }
    int size = 0;
    if (auto* data = findResource (builtInName, size))
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (
            std::make_unique<juce::MemoryInputStream> (data, (size_t) size, false)));
        return decode (reader.get()); // Keep any user-file error visible.
    }
    errorOut = u8 ("内蔵IRが見つかりません");
    return {};
}

//==============================================================================
void ShellSpaceProcessor::reloadBodyIR()
{
    // prepareToPlay(ホストスレッド)と handleAsyncUpdate(メッセージスレッド)が
    // 同時に入ると formatManager と Convolution を並行して触ることになる。
    const juce::ScopedLock sl (irLoadLock);

    const int index = (int) apvts.getRawParameterValue ("bodyType")->load();
    const int material = (int) apvts.getRawParameterValue ("bodyMaterial")->load();
    const int kit = (int) apvts.getRawParameterValue ("bodyKit")->load();
    const float semitones = apvts.getRawParameterValue ("bodyTune")->load();

    double irRate = 48000.0;
    juce::String err;
    auto src = readIR (getUserIR (true), builtInBodyFile (juce::jlimit (0, 4, index), material, kit),
                       irRate, err);

    {
        const juce::ScopedLock sl (stateLock);
        bodyIRError = err;
    }

    if (src.getNumSamples() <= 0)
        return;

    // Time scaling links pitch and decay; this is not physical drum tuning.
    const double ratio = std::pow (2.0, (double) semitones / 12.0);
    const int srcLen = src.getNumSamples();
    const int numCh  = juce::jmin (2, src.getNumChannels());
    const int dstLen = juce::jmax (16, (int) std::floor ((double) srcLen / ratio));
    bodyTailSeconds = dstLen / irRate;

    juce::AudioBuffer<float> dst (juce::jmax (1, numCh), dstLen);

    for (int ch = 0; ch < dst.getNumChannels(); ++ch)
    {
        const auto* in = src.getReadPointer (juce::jmin (ch, src.getNumChannels() - 1));
        auto* outp = dst.getWritePointer (ch);

        for (int j = 0; j < dstLen; ++j)
        {
            const double pos = (double) j * ratio;
            const int i0 = (int) pos;
            const int i1 = juce::jmin (i0 + 1, srcLen - 1);
            const float t = (float) (pos - (double) i0);
            outp[j] = i0 < srcLen ? juce::jmap (t, in[i0], in[i1]) : 0.0f;
        }
    }

    PendingIR next;
    next.first = std::move (dst);
    next.rate = irRate;
    next.ready = true;
    {
        const juce::SpinLock::ScopedLockType mailboxLock (pendingIRLock);
        std::swap (pendingBody, next);
    } // Any superseded buffer is freed here, never on the audio thread.
}

void ShellSpaceProcessor::reloadSpaceIR()
{
    const juce::ScopedLock sl (irLoadLock);

    const int index = juce::jlimit (0, 1, (int) apvts.getRawParameterValue ("spaceType")->load());

    // モノラルのバスでは True Stereo が成立しない（左右の音源を分けられない）。
    // ここで落としておかないと、processChunk が通常ステレオ側へ分岐するのに
    // spaceConv へIRを読んでいない状態になり、JUCEのConvolutionが入力を
    // 素通しして「残響のかわりに原音が返る」。
    const bool stereoBus = outputChannels.load() >= 2;
    const bool wantTrueStereo = apvts.getRawParameterValue ("trueStereo")->load() > 0.5f
                                  && stereoBus;

    double irRate = 48000.0;
    juce::String err;

    const auto userFile = getUserIR (false);
    auto src = readIR (userFile,
                       wantTrueStereo ? kSpaceFiles4ch[index] : kSpaceFiles[index],
                       irRate, err);

    if (src.getNumSamples() <= 0)
    {
        const juce::ScopedLock sl (stateLock);
        spaceIRError = err;
        return;
    }

    const bool have4ch = src.getNumChannels() >= 4;
    spaceTailSeconds = src.getNumSamples() / irRate;
    const bool useTrueStereo = wantTrueStereo && have4ch;

    if (wantTrueStereo && ! have4ch)
        err = u8 ("True Stereoには4chのIRが必要です（")
                + juce::String (src.getNumChannels()) + u8 ("ch）");

    {
        const juce::ScopedLock sl (stateLock);
        spaceIRError = err;
    }

    PendingIR next;
    next.first = extractPair (src, 0);
    next.rate = irRate;
    next.ready = true;
    next.trueStereo = useTrueStereo;
    if (useTrueStereo)
    {
        next.second = extractPair (src, 2);
    }
    {
        const juce::SpinLock::ScopedLockType mailboxLock (pendingIRLock);
        std::swap (pendingSpace, next);
    }
}

void ShellSpaceProcessor::applyPendingIRs()
{
    const juce::SpinLock::ScopedTryLockType mailboxLock (pendingIRLock);
    if (! mailboxLock.isLocked()) return; // Keep the current IR; never wait.
    auto load = [] (juce::dsp::Convolution& conv, juce::AudioBuffer<float>& data, double rate)
    {
        conv.loadImpulseResponse (std::move (data), rate,
                                  juce::dsp::Convolution::Stereo::yes,
                                  juce::dsp::Convolution::Trim::no,
                                  juce::dsp::Convolution::Normalise::yes);
    };
    if (pendingBody.ready)
    {
        load (bodyConv, pendingBody.first, pendingBody.rate);
        pendingBody.ready = false;
    }
    if (pendingSpace.ready)
    {
        if (pendingSpace.trueStereo)
        {
            load (spaceConvL, pendingSpace.first, pendingSpace.rate);
            load (spaceConvR, pendingSpace.second, pendingSpace.rate);
        }
        else load (spaceConv, pendingSpace.first, pendingSpace.rate);
        trueStereoActive = pendingSpace.trueStereo;
        pendingSpace.ready = false;
    }
}

//==============================================================================
void ShellSpaceProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Hosts serialize prepare/process. Workers only prepare mailbox buffers.
    const juce::ScopedLock sl (irLoadLock);

    currentSampleRate = sampleRate;
    outputChannels = getTotalNumOutputChannels();

    juce::dsp::ProcessSpec spec { sampleRate,
                                  (juce::uint32) samplesPerBlock,
                                  (juce::uint32) juce::jmax (1, getTotalNumOutputChannels()) };

    // Unused routes start silent, never as JUCE's default identity IR.
    for (auto* c : { &bodyConv, &spaceConv, &spaceConvL, &spaceConvR })
    {
        juce::AudioBuffer<float> silence (2, 1);
        silence.clear();
        c->loadImpulseResponse (std::move (silence), sampleRate,
            juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::no,
            juce::dsp::Convolution::Normalise::no);
    }
    reloadBodyIR();
    reloadSpaceIR();
    applyPendingIRs();
    // prepare after load guarantees that offline rendering starts with the IR.
    for (auto* c : { &bodyConv, &spaceConv, &spaceConvL, &spaceConvR })
        c->prepare (spec);

    predelay.prepare (spec);
    *wetHighpass.state = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass (
        sampleRate, apvts.getRawParameterValue ("hpf")->load());
    wetHighpass.prepare (spec);

    predelay.reset();
    wetHighpass.reset();
    lastHpHz = -1.0f;

    // 現在値から始めて、以後は滑らかに追わせる（起動時にスイープしないように）
    const float preMs = apvts.getRawParameterValue ("spacePre")->load();
    predelaySamples.reset (sampleRate, 0.05);
    predelaySamples.setCurrentAndTargetValue ((float) (preMs * 0.001 * sampleRate));

    hpfHz.reset (sampleRate, 0.05);
    hpfHz.setCurrentAndTargetValue (apvts.getRawParameterValue ("hpf")->load());

    const int ch = (int) spec.numChannels;
    for (auto* b : { &bodyBuf, &spaceBuf, &spaceBufL, &spaceBufR, &wetBuf })
        b->setSize (ch, samplesPerBlock, false, false, true);

    for (auto* s : { &gDry, &gBody, &gSpace, &gOut, &gBypass })
        s->reset (sampleRate, 0.02);

    reset();
}

void ShellSpaceProcessor::reset()
{
    // Only the host/audio thread touches convolution state.

    // トランスポート移動時などに残響が残らないようにする。
    // これが無いと、切ってあったセクションのレベルを上げた瞬間に
    // 前の入力の尻尾が頭から出てくる。
    for (auto* c : { &bodyConv, &spaceConv, &spaceConvL, &spaceConvR })
        c->reset();

    predelay.reset();
    wetHighpass.reset();

    for (auto* b : { &bodyBuf, &spaceBuf, &spaceBufL, &spaceBufR, &wetBuf })
        b->clear();

    auto gain = [this] (const char* level, const char* mute)
    {
        return apvts.getRawParameterValue (mute)->load() > 0.5f ? 0.0f
             : dbToGain (apvts.getRawParameterValue (level)->load());
    };
    gDry.setCurrentAndTargetValue (gain ("dry", "dryMute"));
    gBody.setCurrentAndTargetValue (gain ("bodyLevel", "bodyMute"));
    gSpace.setCurrentAndTargetValue (gain ("spaceLevel", "spaceMute"));
    gOut.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("out")->load()));
    gBypass.setCurrentAndTargetValue (apvts.getRawParameterValue ("bypass")->load() > 0.5f ? 1.0f : 0.0f);
    bypassWasActive = false;
    predelaySamples.setCurrentAndTargetValue (
        apvts.getRawParameterValue ("spacePre")->load() * 0.001f * (float) currentSampleRate);
    hpfHz.setCurrentAndTargetValue (apvts.getRawParameterValue ("hpf")->load());
    lastHpHz = -1.0f;
    bodyWasActive = spaceWasActive = false;
}

bool ShellSpaceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void ShellSpaceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processAudio (buffer, false);
}

void ShellSpaceProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processAudio (buffer, true);
}

void ShellSpaceProcessor::processAudio (juce::AudioBuffer<float>& buffer, bool hostBypass)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (buffer.getNumChannels(), bodyBuf.getNumChannels());
    const int total = buffer.getNumSamples();

    if (numCh <= 0 || total <= 0)
        return;

    applyPendingIRs();

    gBypass.setTargetValue (hostBypass || apvts.getRawParameterValue ("bypass")->load() > 0.5f ? 1.0f : 0.0f);
    if (gBypass.getCurrentValue() == 1.0f && gBypass.getTargetValue() == 1.0f)
    {
        if (! bypassWasActive)
        {
            reset(); // Discard the faded-out history before suspending DSP.
            gBypass.setCurrentAndTargetValue (1.0f);
        }
        bypassWasActive = true;
        return;
    }
    bypassWasActive = false;

    // ミュートはゲイン0にするだけ。SmoothedValue が滑らかに落とすのでプチらない。
    auto muted = [this] (const char* id) { return apvts.getRawParameterValue (id)->load() > 0.5f; };

    gDry  .setTargetValue (muted ("dryMute")   ? 0.0f : dbToGain (apvts.getRawParameterValue ("dry")->load()));
    gBody .setTargetValue (muted ("bodyMute")  ? 0.0f : dbToGain (apvts.getRawParameterValue ("bodyLevel")->load()));
    gSpace.setTargetValue (muted ("spaceMute") ? 0.0f : dbToGain (apvts.getRawParameterValue ("spaceLevel")->load()));
    gOut  .setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("out")->load()));

    // ホストが prepareToPlay で伝えたブロック長を超えて呼んでくることがある。
    // 内部バッファとdsp側(Convolution/IIR)の上限を超えないよう分割する。
    const int maxChunk = bodyBuf.getNumSamples();
    if (maxChunk <= 0)
        return;

    for (int offset = 0; offset < total; offset += maxChunk)
        processChunk (buffer, numCh, offset, juce::jmin (maxChunk, total - offset));
}

void ShellSpaceProcessor::processChunk (juce::AudioBuffer<float>& buffer, int numCh,
                                        int offset, int n)
{
    auto processConv = [numCh, n] (juce::dsp::Convolution& conv, juce::AudioBuffer<float>& buf)
    {
        juce::dsp::AudioBlock<float> block (buf.getArrayOfWritePointers(),
                                            (size_t) numCh, (size_t) n);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        conv.process (ctx);
    };

    // 無音のセクションは畳み込まない。既定はBODY/SPACEとも-60dB(切)なので、
    // ここを飛ばせば挿しただけの状態のコストがほぼ消える。
    // 音量がゼロへ落ち切るまでは処理を続ける（尾を途中で切らないため）。
    const bool bodyActive  = gBody .getCurrentValue() > 0.0f || gBody .getTargetValue() > 0.0f;
    const bool spaceActive = gSpace.getCurrentValue() > 0.0f || gSpace.getTargetValue() > 0.0f;

    // ---- BODY: 胴鳴り ------------------------------------------------------
    if (bodyActive)
    {
        for (int ch = 0; ch < numCh; ++ch)
            bodyBuf.copyFrom (ch, 0, buffer, ch, offset, n);

        processConv (bodyConv, bodyBuf);
    }
    else
    {
        bodyBuf.clear (0, n);
        if (bodyWasActive) bodyConv.reset();
    }
    bodyWasActive = bodyActive;

    // ---- SPACE: プリディレイ -> ホール --------------------------------------
    // 遅延量は1サンプルずつ滑らかに動かす。いきなり読み取り位置を飛ばすと
    // プチッと鳴るため。テープディレイのように滑らかに移る。
    const float preMs = apvts.getRawParameterValue ("spacePre")->load();
    predelaySamples.setTargetValue ((float) (preMs * 0.001 * currentSampleRate));

    if (! spaceActive)
    {
        if (spaceWasActive)
        {
            spaceConv.reset();
            spaceConvL.reset();
            spaceConvR.reset();
            predelay.reset();
        }
        // 無音なら畳み込みもディレイラインも回さない。
        // 目標値だけは追わせておく（再開時にスイープしないように）。
        predelaySamples.skip (n);
        spaceBuf.clear (0, n);
        spaceWasActive = false;
    }
    else
    {
        spaceWasActive = true;

        // Advance once per sample, shared by both channels.
        for (int i = 0; i < n; ++i)
        {
            predelay.setDelay (predelaySamples.getNextValue());
            for (int ch = 0; ch < numCh; ++ch)
            {
                predelay.pushSample (ch, buffer.getSample (ch, offset + i));
                spaceBuf.setSample (ch, i, predelay.popSample (ch));
            }
        }

        // trueStereoActive はモノラルのバスでは立たない（reloadSpaceIRで落としている）。
        // 念のためここでも確認し、条件が崩れたらIR未ロードの畳み込みを通さないようにする。
        if (trueStereoActive.load() && numCh >= 2 && spaceBufL.getNumChannels() >= 2)
        {
            // True Stereo: 左入力を LL/LR に、右入力を RL/RR に通して足す。
            // 各系統は入力を両chに複製して渡す（IRの2chがそれぞれの行き先）。
            for (int ch = 0; ch < 2; ++ch)
            {
                spaceBufL.copyFrom (ch, 0, spaceBuf, 0, 0, n);   // 左入力
                spaceBufR.copyFrom (ch, 0, spaceBuf, 1, 0, n);   // 右入力
            }

            processConv (spaceConvL, spaceBufL);
            processConv (spaceConvR, spaceBufR);

            for (int ch = 0; ch < 2; ++ch)
            {
                spaceBuf.copyFrom (ch, 0, spaceBufL, ch, 0, n);
                spaceBuf.addFrom  (ch, 0, spaceBufR, ch, 0, n);
            }
        }
        else
        {
            processConv (spaceConv, spaceBuf);
        }
    }

    // ---- WET をまとめて HPF -------------------------------------------------
    wetBuf.clear (0, n);
    for (int i = 0; i < n; ++i)
    {
        const auto bodyGain = gBody.getNextValue();
        const auto spaceGain = gSpace.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
            wetBuf.setSample (ch, i, bodyBuf.getSample (ch, i) * bodyGain
                                    + spaceBuf.getSample (ch, i) * spaceGain);
    }

    // カットオフを滑らかに追わせ、ブロックごとに係数を作り直す。
    // 目標値へ一気に飛ばすと係数が跳ねて段付きノイズになる。
    hpfHz.setTargetValue (apvts.getRawParameterValue ("hpf")->load());
    const float hp = hpfHz.skip (n);

    if (! juce::approximatelyEqual (hp, lastHpHz))
    {
        *wetHighpass.state = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass (
                                 currentSampleRate, juce::jlimit (20.0f, 400.0f, hp));
        lastHpHz = hp;
    }
    {
        juce::dsp::AudioBlock<float> block (wetBuf.getArrayOfWritePointers(),
                                            (size_t) numCh, (size_t) n);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        wetHighpass.process (ctx);
    }

    // ---- DRY + WET ---------------------------------------------------------
    for (int i = 0; i < n; ++i)
    {
        const auto dryGain = gDry.getNextValue();
        const auto outputGain = gOut.getNextValue();
        const auto bypassMix = gBypass.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
        {
            const auto input = buffer.getSample (ch, offset + i);
            const auto processed = (input * dryGain + wetBuf.getSample (ch, i)) * outputGain;
            buffer.setSample (ch, offset + i,
                              input * bypassMix + processed * (1.0f - bypassMix));
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* ShellSpaceProcessor::createEditor()
{
    return new ShellSpaceEditor (*this);
}

void ShellSpaceProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("currentProgram", currentProgram.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ShellSpaceProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (! xml->hasTagName ("STATE")) return;
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
        currentProgram = juce::jlimit (0, kNumPresets - 1,
                                     (int) apvts.state.getProperty ("currentProgram", 0));

        // replaceState は「ツリー上の値が変化したパラメータ」にしか通知しない。
        // Bool/Choice のような離散パラメータは生の正規化値をスナップせずに保持するため
        // (0.31 を渡すと 0.31 のまま持ち、判定時だけ >= 0.5 を見る)、
        // ツリー上は同じ 0 でもパラメータ側に 0.31 が残り、保存->復元で値が一致しない。
        // ツリーの値を明示的に押し込んで揃える。
        for (int i = 0; i < apvts.state.getNumChildren(); ++i)
        {
            const auto child = apvts.state.getChild (i);
            const auto id = child.getProperty ("id").toString();

            if (id.isEmpty() || ! child.hasProperty ("value"))
                continue;

            if (auto* param = apvts.getParameter (id))
            {
                const float raw = (float) child.getProperty ("value");
                param->setValueNotifyingHost (param->convertTo0to1 (raw));
            }
        }
    }

    // 復元した state からユーザーIRのパスをキャッシュへ写す。
    // これを忘れると、復元後のリロードが古いパスを見る。
    refreshUserIRPaths();

    bodyDirty = true;
    spaceDirty = true;
    triggerAsyncUpdate();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ShellSpaceProcessor();
}
