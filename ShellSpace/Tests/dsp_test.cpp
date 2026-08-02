// プラグインを直接リンクして、DSPの頑健性を確かめる。
// VST3ラッパーを通さないので、問題が自分のコードにあるのかラッパー側かを切り分けられる。
//
//   ShellSpaceDspTest.exe

#include "../Source/PluginProcessor.h"

#include <iostream>
#include <iomanip>
#include <cmath>

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what, const juce::String& detail = {})
    {
        std::cout << (ok ? "  [OK]   " : "  [FAIL] ") << what;
        if (detail.isNotEmpty())
            std::cout << "  -- " << detail;
        std::cout << std::endl;
        if (! ok)
            ++failures;
    }

    void setNorm (ShellSpaceProcessor& p, const juce::String& id, float v)
    {
        if (auto* param = p.apvts.getParameter (id))
            param->setValueNotifyingHost (v);
    }

    struct Stats { float peak = 0.0f; bool finite = true; };

    Stats scan (const juce::AudioBuffer<float>& b)
    {
        Stats s;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float v = b.getSample (ch, i);
                if (! std::isfinite (v)) s.finite = false;
                s.peak = juce::jmax (s.peak, std::abs (v));
            }
        return s;
    }

    /** juce::dsp::Convolution はIRをバックグラウンドスレッドで読む。
        待たずに処理を始めると空のIRで畳み込まれて完全な無音になる。
        無音を流しながら読み込み完了を待つ。 */
    void warmUp (ShellSpaceProcessor& p, double sr, int blockSize)
    {
        const int numCh = juce::jmax (1, p.getTotalNumOutputChannels());
        juce::AudioBuffer<float> silence (numCh, blockSize);
        juce::MidiBuffer midi;

        const int blocksPer100ms = juce::jmax (1, (int) (sr * 0.1 / blockSize));

        for (int pass = 0; pass < 20; ++pass)
        {
            juce::Thread::sleep (100);

            for (int b = 0; b < blocksPer100ms; ++b)
            {
                silence.clear();
                p.processBlock (silence, midi);
            }
        }
        p.reset();
    }

    /** BODYだけ鳴らす設定にする */
    void configureBodyOnly (ShellSpaceProcessor& p)
    {
        setNorm (p, "dry",        0.0f);           // -60dB
        setNorm (p, "spaceLevel", 0.0f);           // -60dB
        setNorm (p, "bodyLevel",  60.0f / 72.0f);  // 0dB
        setNorm (p, "bodyType",   0.0f);           // Kick
        setNorm (p, "bodyTune",   0.5f);           // 0半音
        setNorm (p, "hpf",        0.0f);
        setNorm (p, "out",        0.5f);           // 0dB
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    constexpr double sr = 48000.0;
    constexpr int prepared = 512;
    constexpr int chunks = 8;
    constexpr int bigSize = prepared * chunks;

    juce::MidiBuffer midi;

    // ---- 1. prepareToPlay より大きいブロックで呼ぶ --------------------------
    std::cout << "== ブロック長超過 (" << prepared << " で prepare して "
              << bigSize << " で呼ぶ) ==" << std::endl;

    juce::AudioBuffer<float> refBuf (2, bigSize);
    juce::AudioBuffer<float> bigBuf (2, bigSize);

    {
        // 基準: 小ブロックで8回に分けて処理
        ShellSpaceProcessor proc;
        proc.prepareToPlay (sr, prepared);
        configureBodyOnly (proc);
        warmUp (proc, sr, prepared);

        refBuf.clear();
        for (int ch = 0; ch < 2; ++ch)
            refBuf.setSample (ch, 0, 1.0f);

        for (int c = 0; c < chunks; ++c)
        {
            juce::AudioBuffer<float> sub (refBuf.getArrayOfWritePointers(), 2,
                                          c * prepared, prepared);
            proc.processBlock (sub, midi);
        }
    }

    {
        // 比較: 同じ入力を1回の大ブロックで処理
        ShellSpaceProcessor proc;
        proc.prepareToPlay (sr, prepared);
        configureBodyOnly (proc);
        warmUp (proc, sr, prepared);

        bigBuf.clear();
        for (int ch = 0; ch < 2; ++ch)
            bigBuf.setSample (ch, 0, 1.0f);

        proc.processBlock (bigBuf, midi);   // 分割処理していないとここで壊れる
    }

    const auto refStats = scan (refBuf);
    const auto bigStats = scan (bigBuf);

    float maxDiff = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < bigSize; ++i)
            maxDiff = juce::jmax (maxDiff,
                                  std::abs (refBuf.getSample (ch, i) - bigBuf.getSample (ch, i)));

    std::cout << "  小ブロック8回: peak=" << std::fixed << std::setprecision (6) << refStats.peak
              << "   大ブロック1回: peak=" << bigStats.peak << std::endl;
    std::cout << "  最大差: " << std::scientific << maxDiff << std::endl;

    check (bigStats.finite && refStats.finite, "NaN/Infが出ない");
    check (bigStats.peak > 0.0001f, "大ブロックでも出力がある");
    check (maxDiff < 1.0e-6f, "分割処理の結果が小ブロック処理と一致する",
           "maxDiff=" + juce::String (maxDiff, 9));

    // ---- 2. 別のサンプルレート ---------------------------------------------
    for (double rate : { 44100.0, 96000.0 })
    {
        std::cout << "\n== " << (int) rate << " Hz ==" << std::endl;

        ShellSpaceProcessor proc;
        proc.prepareToPlay (rate, prepared);
        configureBodyOnly (proc);
        warmUp (proc, rate, prepared);

        juce::AudioBuffer<float> block (2, prepared);
        Stats total;

        for (int b = 0; b < (int) (rate / prepared); ++b)
        {
            block.clear();
            if (b == 0)
                for (int ch = 0; ch < 2; ++ch)
                    block.setSample (ch, 0, 1.0f);

            proc.processBlock (block, midi);

            const auto s = scan (block);
            total.peak = juce::jmax (total.peak, s.peak);
            total.finite = total.finite && s.finite;
        }

        std::cout << "  peak=" << std::fixed << std::setprecision (6) << total.peak << std::endl;
        check (total.finite, juce::String ((int) rate) + "Hz でNaN/Infが出ない");
        check (total.peak > 0.001f, juce::String ((int) rate) + "Hz でも音が出る");
    }

    // ---- 3. モノラル --------------------------------------------------------
    std::cout << "\n== モノラル (1in/1out) ==" << std::endl;
    {
        ShellSpaceProcessor proc;

        juce::AudioProcessor::BusesLayout mono;
        mono.inputBuses .add (juce::AudioChannelSet::mono());
        mono.outputBuses.add (juce::AudioChannelSet::mono());

        const bool accepted = proc.setBusesLayout (mono);
        check (accepted, "モノラル構成を受け付ける");

        if (accepted)
        {
            proc.prepareToPlay (sr, prepared);
            configureBodyOnly (proc);
            warmUp (proc, sr, prepared);

            juce::AudioBuffer<float> block (1, prepared);
            Stats total;

            for (int b = 0; b < (int) (sr / prepared); ++b)
            {
                block.clear();
                if (b == 0)
                    block.setSample (0, 0, 1.0f);

                proc.processBlock (block, midi);

                const auto s = scan (block);
                total.peak = juce::jmax (total.peak, s.peak);
                total.finite = total.finite && s.finite;
            }

            std::cout << "  peak=" << std::fixed << std::setprecision (6) << total.peak << std::endl;
            check (total.finite, "モノラルでNaN/Infが出ない");
            check (total.peak > 0.001f, "モノラルでも音が出る");
        }
    }

    std::cout << "\n================================" << std::endl;
    std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
