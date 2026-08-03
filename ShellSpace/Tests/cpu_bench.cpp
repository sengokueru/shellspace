// CPU負荷を実測する。
// 「重いかどうか」を体感や推測でなく、リアルタイム比で出す。
//
//   ShellSpaceCpuBench.exe
//
// リアルタイム比 = かかったCPU時間 / 処理した音の長さ
//   0.01 なら 1コアの1%。DAWで100本挿して1コア分。

#include "../Source/PluginProcessor.h"

#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>

namespace
{
    constexpr double kSR = 48000.0;
    constexpr double kSeconds = 10.0;

    void setNorm (ShellSpaceProcessor& p, const juce::String& id, float v)
    {
        if (auto* param = p.apvts.getParameter (id))
            param->setValueNotifyingHost (v);
    }

    struct Result { double realtimeRatio; double msPerBlock; };

    Result measureOnce (int blockSize, bool body, bool space, bool trueStereo, bool bypass)
    {
        ShellSpaceProcessor proc;

        // 設定してから prepareToPlay。メッセージループを回さないので
        // prepareToPlay の同期リロードに反映させる必要がある。
        setNorm (proc, "dry",        0.909f);
        setNorm (proc, "bodyLevel",  body  ? 60.0f / 72.0f : 0.0f);
        setNorm (proc, "spaceLevel", space ? 60.0f / 72.0f : 0.0f);
        setNorm (proc, "trueStereo", trueStereo ? 1.0f : 0.0f);
        setNorm (proc, "out",        0.5f);
        setNorm (proc, "bypass",     bypass ? 1.0f : 0.0f);

        proc.prepareToPlay (kSR, blockSize);

        // IRはバックグラウンドで読まれるので、始まるまで待つ
        {
            juce::AudioBuffer<float> warm (2, blockSize);
            juce::MidiBuffer midi;
            for (int pass = 0; pass < 15; ++pass)
            {
                juce::Thread::sleep (100);
                for (int b = 0; b < (int) (kSR * 0.1 / blockSize); ++b)
                {
                    warm.clear();
                    proc.processBlock (warm, midi);
                }
            }
            proc.reset();
        }

        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer midi;
        juce::Random rng (1234);

        const int blocks = (int) (kSR * kSeconds / blockSize);

        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = buf.getWritePointer (ch);
                for (int i = 0; i < blockSize; ++i)
                    d[i] = rng.nextFloat() * 0.5f - 0.25f;
            }
            proc.processBlock (buf, midi);
        }
        const auto t1 = std::chrono::steady_clock::now();

        const double sec = std::chrono::duration<double> (t1 - t0).count();
        const double audioSec = blocks * blockSize / kSR;

        return { sec / audioSec, sec / blocks * 1000.0 };
    }

    /** 何度か測って最小値を採る。
        1回だけだとOSのスケジューリングや他プロセスの影響で±30%ぶれる。
        最小値は「邪魔が入らなかった回」に相当し、CPUコストの推定として最も安定する。 */
    Result measure (int blockSize, bool body, bool space, bool trueStereo, bool bypass = false)
    {
        Result best { 1e9, 1e9 };
        for (int i = 0; i < 3; ++i)
        {
            const auto r = measureOnce (blockSize, body, space, trueStereo, bypass);
            if (r.realtimeRatio < best.realtimeRatio)
                best = r;
        }
        return best;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "ShellSpace CPU負荷 (48kHz, " << kSeconds << "秒ぶんを処理)" << std::endl;
    std::cout << "リアルタイム比 = CPU時間 / 音の長さ。0.01 = 1コアの1%" << std::endl;
    std::cout << std::endl;

    struct Case { const char* name; bool body, space, ts, bypass; };
    const Case cases[] = {
        // 測定器の妥当性確認。バイパスは即returnするので、ほぼ0でなければ
        // ベンチ自体が何かを測ってしまっている
        { "[基準] バイパス",          false, false, false, true  },
        { "全部OFF (既定)",           false, false, false, false },
        { "BODY のみ",                true,  false, false, false },
        { "SPACE のみ (通常ステレオ)", false, true,  false, false },
        { "SPACE のみ (True Stereo)", false, true,  true,  false },
        { "BODY + SPACE",             true,  true,  false, false },
        { "BODY + SPACE(True St.)",   true,  true,  true,  false },
    };

    for (int blockSize : { 64, 128, 512 })
    {
        std::cout << "=== ブロック長 " << blockSize << " ===" << std::endl;
        std::cout << std::left << std::setw (26) << "構成"
                  << std::right << std::setw (12) << "リアルタイム比"
                  << std::setw (14) << "1ブロック(ms)"
                  << std::setw (10) << "同時本数" << std::endl;

        for (const auto& c : cases)
        {
            const auto r = measure (blockSize, c.body, c.space, c.ts, c.bypass);
            const int instances = r.realtimeRatio > 0.0 ? (int) (1.0 / r.realtimeRatio) : 0;

            std::cout << std::left << std::setw (26) << c.name
                      << std::right << std::fixed << std::setprecision (4)
                      << std::setw (12) << r.realtimeRatio
                      << std::setprecision (3) << std::setw (14) << r.msPerBlock
                      << std::setw (10) << instances << std::endl;
        }
        std::cout << std::endl;
    }

    std::cout << "「同時本数」は1コアを埋めるのに必要な本数の目安。" << std::endl;
    return 0;
}
