// ビルドしたVST3を実際にロードして音を通し、出力を測る検証ホスト。
// 「ビルドが通った」と「音が出る」は別物なので、ここまでやって初めて確認したと言える。
//
//   ShellSpaceTest.exe <path to ShellSpace.vst3>

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>

#if JUCE_WINDOWS
 #include <windows.h>   // SetConsoleOutputCP。JUCEは windows.h を露出させない
#endif

namespace
{
    constexpr double kSR = 48000.0;
    constexpr int    kBlock = 512;
    constexpr double kCaptureSeconds = 2.0;

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

    juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& p, const juce::String& name)
    {
        for (auto* param : p.getParameters())
            if (param->getName (64) == name)
                return param;
        return nullptr;
    }

    bool setParam (juce::AudioPluginInstance& p, const juce::String& name, float normalised)
    {
        if (auto* param = findParam (p, name))
        {
            param->setValueNotifyingHost (normalised);
            return true;
        }
        std::cout << "  [FAIL] パラメータが見つかりません: " << name << std::endl;
        ++failures;
        return false;
    }

    /** メッセージループを回す。IRの読み込みとTune変更はメッセージスレッド経由なので必要。 */
    void pump (int ms)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    /** 出力の主要周波数。Tuneが効いているかは減衰時間より基音で見るほうが確実。 */
    double dominantHz (const juce::AudioBuffer<float>& buf)
    {
        constexpr int order = 15;              // 32768点 -> 分解能 1.46Hz
        constexpr int fftSize = 1 << order;

        juce::dsp::FFT fft (order);
        std::vector<float> fd ((size_t) fftSize * 2, 0.0f);

        const int n = juce::jmin (fftSize, buf.getNumSamples());
        for (int i = 0; i < n; ++i)
        {
            float v = 0.0f;
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                v += buf.getSample (ch, i);

            const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                    * (float) i / (float) (n - 1));
            fd[(size_t) i] = v * w;
        }

        fft.performFrequencyOnlyForwardTransform (fd.data());

        const double binHz = kSR / (double) fftSize;
        int best = 0;
        float bestV = 0.0f;
        for (int i = (int) (25.0 / binHz); i < fftSize / 2; ++i)
            if (fd[(size_t) i] > bestV) { bestV = fd[(size_t) i]; best = i; }

        return (double) best * binHz;
    }

    /** インパルスを1発入れて出力を捕まえる。
        測る前に内部状態を完全に吐き出させる。ホールIRは2.9秒あるので、
        これをやらないと前のキャプチャの尻尾が頭から出てきて測定が壊れる。 */
    juce::AudioBuffer<float> captureImpulseResponse (juce::AudioPluginInstance& p)
    {
        p.reset();
        {
            juce::AudioBuffer<float> flush (2, kBlock);
            juce::MidiBuffer midi;
            const int flushBlocks = (int) (3.5 * kSR / kBlock);
            for (int i = 0; i < flushBlocks; ++i)
            {
                flush.clear();
                p.processBlock (flush, midi);
            }
        }

        const int total = (int) (kCaptureSeconds * kSR);
        juce::AudioBuffer<float> captured (2, total);
        captured.clear();

        juce::AudioBuffer<float> block (2, kBlock);
        juce::MidiBuffer midi;

        for (int pos = 0; pos < total; pos += kBlock)
        {
            const int n = juce::jmin (kBlock, total - pos);
            block.clear();

            if (pos == 0)
                for (int ch = 0; ch < 2; ++ch)
                    block.setSample (ch, 0, 1.0f);

            juce::AudioBuffer<float> sub (block.getArrayOfWritePointers(), 2, 0, n);
            p.processBlock (sub, midi);

            for (int ch = 0; ch < 2; ++ch)
                captured.copyFrom (ch, pos, sub, ch, 0, n);
        }

        return captured;
    }

    struct Measurement
    {
        float peak = 0.0f;
        double peakTimeMs = 0.0;
        double decayMs = 0.0;   // ピークから -20dB まで
        double onsetMs = 0.0;   // 最初にピークの1%を超えた時刻
    };

    Measurement measure (const juce::AudioBuffer<float>& buf)
    {
        Measurement m;
        const int n = buf.getNumSamples();
        std::vector<float> env ((size_t) n, 0.0f);

        for (int i = 0; i < n; ++i)
        {
            float v = 0.0f;
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                v = juce::jmax (v, std::abs (buf.getSample (ch, i)));
            env[(size_t) i] = v;

            if (v > m.peak) { m.peak = v; m.peakTimeMs = i / kSR * 1000.0; }
        }

        if (m.peak <= 0.0f)
            return m;

        for (int i = 0; i < n; ++i)
            if (env[(size_t) i] > m.peak * 0.01f) { m.onsetMs = i / kSR * 1000.0; break; }

        // ピーク以降で -20dB を継続的に下回る点
        const float threshold = m.peak * 0.1f;
        const int peakIdx = (int) (m.peakTimeMs / 1000.0 * kSR);
        int below = 0;
        for (int i = peakIdx; i < n; ++i)
        {
            if (env[(size_t) i] < threshold)
            {
                if (++below > (int) (0.02 * kSR))   // 20ms continuous
                {
                    m.decayMs = (i - below) / kSR * 1000.0 - m.peakTimeMs;
                    break;
                }
            }
            else below = 0;
        }
        return m;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

   #if JUCE_WINDOWS
    // 日本語版Windowsのコンソールは既定CP932。UTF-8で出すので明示する。
    SetConsoleOutputCP (CP_UTF8);
   #endif

    if (argc < 2)
    {
        std::cout << "usage: ShellSpaceTest <path to ShellSpace.vst3>" << std::endl;
        return 2;
    }

    const juce::File pluginFile (juce::String::fromUTF8 (argv[1]));
    std::cout << "plugin: " << pluginFile.getFullPathName() << std::endl;

    if (! pluginFile.exists())
    {
        std::cout << "  [FAIL] ファイルがありません" << std::endl;
        return 1;
    }

    juce::VST3PluginFormat vst3;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    vst3.findAllTypesForFile (descriptions, pluginFile.getFullPathName());

    std::cout << "\n== ロード ==" << std::endl;
    check (! descriptions.isEmpty(), "VST3として認識される",
           "found " + juce::String (descriptions.size()));
    if (descriptions.isEmpty())
        return 1;

    // JUCE 8 で addDefaultFormats() は削除された。VST3だけ要るので直接登録する。
    juce::AudioPluginFormatManager fm;
    fm.addFormat (new juce::VST3PluginFormat());

    juce::String error;
    auto instance = fm.createPluginInstance (*descriptions[0], kSR, kBlock, error);
    check (instance != nullptr, "インスタンス生成", error);
    if (instance == nullptr)
        return 1;

    std::cout << "  name=" << instance->getName()
              << "  in=" << instance->getTotalNumInputChannels()
              << "  out=" << instance->getTotalNumOutputChannels() << std::endl;

    std::cout << "\n== パラメータ ==" << std::endl;
    const juce::StringArray expected {
        "Body Type", "Body Tune", "Body Level",
        "Space Type", "Predelay", "Space Level",
        "Wet HPF", "Dry", "Output" };

    for (const auto& name : expected)
    {
        auto* param = findParam (*instance, name);
        check (param != nullptr, "パラメータ " + name,
               param != nullptr ? param->getCurrentValueAsText() : juce::String());
    }

    // ---- VST3ラッパー経由でエディタが開くか --------------------------------
    // ui_snapshot はJUCEコンポーネントを直接描いているので、ここは別の経路。
    // ラッパーがサイズを正しく伝えないとホスト上で窓が切れる。
    std::cout << "\n== エディタ (VST3ラッパー経由) ==" << std::endl;
    check (instance->hasEditor(), "エディタを持っている");

    if (auto* ed = instance->createEditorIfNeeded())
    {
        std::cout << "  size: " << ed->getWidth() << " x " << ed->getHeight() << std::endl;
        check (ed->getWidth() == 322 && ed->getHeight() == 470,
               "エディタサイズが 322x470 で伝わる",
               juce::String (ed->getWidth()) + "x" + juce::String (ed->getHeight()));
        delete ed;   // デストラクタが editorBeingDeleted を呼ぶ
    }
    else
    {
        check (false, "エディタ生成");
    }

    instance->setPlayConfigDetails (2, 2, kSR, kBlock);
    instance->prepareToPlay (kSR, kBlock);
    pump (1500);   // IRのバックグラウンド読み込みを待つ

    // ---- BODY だけ鳴らす -------------------------------------------------
    std::cout << "\n== BODY (Kick, Tune 0) ==" << std::endl;
    setParam (*instance, "Dry",         0.0f);            // -60dB = 無音
    setParam (*instance, "Space Level", 0.0f);            // -60dB = 切
    setParam (*instance, "Body Level",  60.0f / 72.0f);   // 0dB
    setParam (*instance, "Body Type",   0.0f);            // Kick
    setParam (*instance, "Body Tune",   0.5f);            // 0半音
    setParam (*instance, "Wet HPF",     0.0f);            // 20Hz
    setParam (*instance, "Output",      0.5f);            // 0dB
    pump (1500);

    auto body0 = captureImpulseResponse (*instance);
    auto kick0 = measure (body0);
    const double hz0 = dominantHz (body0);
    std::cout << "  peak=" << std::fixed << std::setprecision (4) << kick0.peak
              << "  onset=" << std::setprecision (2) << kick0.onsetMs << "ms"
              << "  -20dB=" << kick0.decayMs << "ms"
              << "  基音=" << std::setprecision (1) << hz0 << "Hz" << std::endl;
    check (kick0.peak > 0.001f, "BODYから音が出る");
    check (kick0.decayMs > 50.0, "胴鳴りが減衰を持つ（50ms以上）");
    check (hz0 > 30.0 && hz0 < 90.0, "キックの基音が30〜90Hzに出る",
           juce::String (hz0, 1) + "Hz");

    // ---- Tune を +12半音: IRが半分の長さ = 基音が2倍になるはず --------------
    std::cout << "\n== BODY (Kick, Tune +12) ==" << std::endl;
    setParam (*instance, "Body Tune", 1.0f);
    pump (2000);

    auto body12 = captureImpulseResponse (*instance);
    auto kick12 = measure (body12);
    const double hz12 = dominantHz (body12);
    const double hzRatio = hz0 > 0.0 ? hz12 / hz0 : 0.0;
    const double decayRatio = kick0.decayMs > 0.0 ? kick12.decayMs / kick0.decayMs : 0.0;
    std::cout << "  peak=" << std::setprecision (4) << kick12.peak
              << "  -20dB=" << std::setprecision (2) << kick12.decayMs << "ms"
              << "  基音=" << std::setprecision (1) << hz12 << "Hz"
              << "  (周波数比 " << std::setprecision (3) << hzRatio
              << " / 減衰比 " << decayRatio << ")" << std::endl;
    check (kick12.peak > 0.001f, "Tune変更後も音が出る");
    check (hzRatio > 1.9 && hzRatio < 2.1,
           "+12半音で基音が2倍になる（IR伸縮が効いている）",
           "ratio=" + juce::String (hzRatio, 3));
    check (decayRatio < 0.8, "減衰も短くなる（IR全体が縮んでいる）",
           "ratio=" + juce::String (decayRatio, 3));

    // ---- SPACE だけ鳴らす -------------------------------------------------
    std::cout << "\n== SPACE (Hall Drum, Predelay 40ms) ==" << std::endl;
    setParam (*instance, "Body Level",  0.0f);             // 切
    setParam (*instance, "Space Level", 60.0f / 72.0f);    // 0dB
    setParam (*instance, "Space Type",  1.0f);             // Hall Drum
    setParam (*instance, "Predelay",    40.0f / 120.0f);   // 40ms
    pump (2000);

    auto hall = measure (captureImpulseResponse (*instance));
    std::cout << "  peak=" << std::setprecision (4) << hall.peak
              << "  onset=" << std::setprecision (2) << hall.onsetMs << "ms"
              << "  -20dB=" << hall.decayMs << "ms" << std::endl;
    check (hall.peak > 0.001f, "SPACEから音が出る");
    check (hall.onsetMs > 35.0 && hall.onsetMs < 70.0,
           "プリディレイ40ms が効いている（立ち上がりが40〜70ms）",
           "onset=" + juce::String (hall.onsetMs, 1) + "ms");
    check (hall.decayMs > 300.0, "ホールの残響が続く（-20dBまで300ms以上）");

    // ---- 全部切ったら無音か ----------------------------------------------
    std::cout << "\n== 全部切る ==" << std::endl;
    setParam (*instance, "Space Level", 0.0f);
    setParam (*instance, "Body Level",  0.0f);
    setParam (*instance, "Dry",         0.0f);
    pump (1000);

    auto silent = measure (captureImpulseResponse (*instance));
    std::cout << "  peak=" << std::setprecision (6) << silent.peak << std::endl;
    check (silent.peak < 0.001f, "全部-60dBで無音になる");

    // ブロック長超過・サンプルレート・モノラルの検証は dsp_test.cpp に置いた。
    // VST3規格では setupProcessing で決めた最大ブロック長を超えて呼ぶのは規約違反で、
    // ここで試すとJUCEのVST3ホストラッパー側が落ちる（プラグインの問題ではない）。

    instance->releaseResources();
    instance.reset();

    std::cout << "\n================================" << std::endl;
    std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED")
              << std::endl;
    return failures == 0 ? 0 : 1;
}
