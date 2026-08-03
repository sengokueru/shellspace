#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>

/** 胴鳴り(BODY)とホール(SPACE)を並列に走らせるコンボリューション。

    - IRは内蔵。ユーザーのwavに差し替えることもできる。
    - SPACEは True Stereo(4ch LL/LR/RL/RR)に対応。畳み込みを2系統使う。
    - BODYはTuneでIRを伸縮させてピッチを合わせる。
*/
class ShellSpaceProcessor  : public juce::AudioProcessor,
                             private juce::AudioProcessorValueTreeState::Listener,
                             private juce::AsyncUpdater,
                             private juce::Timer
{
public:
    ShellSpaceProcessor();
    ~ShellSpaceProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                        { return true; }

    const juce::String getName() const override            { return "ShellSpace"; }
    bool acceptsMidi() const override                      { return false; }
    bool producesMidi() const override                     { return false; }
    bool isMidiEffect() const override                     { return false; }
    double getTailLengthSeconds() const override           { return 3.5; }

    //==============================================================================
    // ファクトリープリセット
    int getNumPrograms() override;
    int getCurrentProgram() override                       { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================
    /** ユーザーIRの差し替え。存在しないFileを渡すと内蔵IRに戻る。
        @param body  true=BODY, false=SPACE */
    void setUserIR (bool body, const juce::File& file);
    juce::File getUserIR (bool body) const;

    /** 直近の読み込みが失敗していればその理由。成功時は空。 */
    juce::String getIRError (bool body) const;

    /** ホスト側のバイパスと連動させる */
    juce::AudioParameterBool* getBypassParameter() const override
    {
        return dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));
    }

    juce::AudioProcessorValueTreeState apvts;

    static constexpr int kEditorWidth  = 448;
    static constexpr int kEditorHeight = 550;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void parameterChanged (const juce::String& id, float newValue) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void reloadBodyIR();
    void reloadSpaceIR();

    /** IR差し替えをまとめるまでの待ち時間(ms)。
        bodyTune は刻み0.01の連続パラメータなので、値が変わるたびに
        loadImpulseResponse を呼ぶとオートメーション時に極端な頻度になる。
        JUCEのConvolutionは内部で std::function のキューを使っており、
        その頻度に耐えられず空のfunctionを呼んで abort する。
        バーストを1回にまとめる。 */
    static constexpr int kIRReloadDelayMs = 120;

    /** ブロックを内部バッファの上限以下に分割して処理する実体。 */
    void processChunk (juce::AudioBuffer<float>& buffer, int numCh, int offset, int len);

    /** 埋め込みリソース or ユーザーファイルからIRを読む。失敗したら空のバッファ。 */
    juce::AudioBuffer<float> readIR (const juce::File& userFile,
                                     const juce::String& builtInName,
                                     double& sampleRateOut,
                                     juce::String& errorOut);

    juce::AudioFormatManager formatManager;

    /** 分割方式は JUCE 既定（均一分割・ゼロレイテンシ）のまま。
        非均一分割(NonUniform{256})も試したが、実測で判断を分けた:
          ブロック長 64  BODY+SPACE  0.893 -> 0.238  (改善)
          ブロック長 512 BODY+SPACE  0.087 -> 0.194  (悪化)
        512 は最もよく使われる設定で、そこで2倍以上重くなるため採用しない。
        ブロック長に応じて head サイズを変える手はあるが、Convolution は
        構築時にしか決められず作り直しが要る。効果と複雑さが釣り合わない。 */
    juce::dsp::Convolution bodyConv;
    juce::dsp::Convolution spaceConv;
    juce::dsp::Convolution spaceConvL;
    juce::dsp::Convolution spaceConvR;

    /** 無音のセクションは畳み込まない。既定はBODY/SPACEとも-60dB(切)なので、
        挿しただけの状態で2系統が無駄に回っていた。 */
    bool bodyWasActive { false }, spaceWasActive { false };

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> predelay { 96000 };
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                   juce::dsp::IIR::Coefficients<float>> wetHighpass;

    juce::AudioBuffer<float> bodyBuf, spaceBuf, spaceBufL, spaceBufR, wetBuf;

    juce::SmoothedValue<float> gDry, gBody, gSpace, gOut;

    /** Predelay と Wet HPF は値が飛ぶと音に出る。
        Predelay: 読み取り位置が瞬間移動してプチッと鳴る
        HPF     : 係数が跳ねて段付きノイズになる
        どちらも目標値を滑らかに追わせて回避する。 */
    juce::SmoothedValue<float> predelaySamples, hpfHz;

    std::atomic<bool> bodyDirty { true }, spaceDirty { true };
    std::atomic<bool> trueStereoActive { false };

    double currentSampleRate { 48000.0 };
    float lastHpHz { -1.0f };
    int currentProgram { 0 };

    /** ユーザーIRのパスと直近のエラー。
        juce::ValueTree はスレッド安全ではないのに、IRの読み込みは
        prepareToPlay(ホストスレッド) と handleAsyncUpdate(メッセージスレッド) の
        両方から走る。apvts.state を直接読みに行かず、ここに写しておく。 */
    juce::String userBodyIRPath, userSpaceIRPath;
    juce::String bodyIRError, spaceIRError;
    juce::CriticalSection stateLock;

    /** apvts.state の内容を上のキャッシュへ写す。メッセージスレッドから呼ぶこと。 */
    void refreshUserIRPaths();

    /** IRの読み込みを直列化する。
        reloadBodyIR/reloadSpaceIR は prepareToPlay(ホストスレッド)と
        handleAsyncUpdate(メッセージスレッド)の両方から走るため、同時に入ると
        formatManager と Convolution を並行して触ることになる。
        どちらも実時間スレッドではないのでロックして構わない。 */
    juce::CriticalSection irLoadLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShellSpaceProcessor)
};
