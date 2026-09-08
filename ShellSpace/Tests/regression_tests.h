#pragma once
#include "../Source/PluginEditor.h"
#include <thread>

static void runAutomationStress()
{
    ShellSpaceProcessor p;
    configureBodyOnly (p);
    p.prepareToPlay (48000, 128);
    std::atomic<bool> running { true }, finite { true };
    std::atomic<int> processed { 0 };
    std::thread audioThread ([&]
    {
        juce::AudioBuffer<float> block (2, 128);
        juce::MidiBuffer midi;
        while (running.load())
        {
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill (block.getWritePointer (ch), 0.01f, 128);
            p.processBlock (block, midi);
            if (! scan (block).finite) finite = false;
            ++processed;
            juce::Thread::sleep (1);
        }
    });
    for (int i = 0; i < 80; ++i)
    {
        setNorm (p, "bodyTune", (float) (i % 21) / 20.0f);
        setNorm (p, "bodyType", (float) (i % 5) / 4.0f);
        setNorm (p, "trueStereo", (float) (i % 2));
        setNorm (p, "spaceLevel", 0.7f);
        setNorm (p, "spacePre", (float) (i % 11) / 10.0f);
        setNorm (p, "hpf", (float) (i % 11) / 10.0f);
        if (i % 10 == 0)
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
            editor->resized();
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (25);
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil (250);
    running = false;
    audioThread.join();
    check (finite && processed > 100, "Concurrent IR automation and editor lifecycle remain finite",
           "blocks=" + juce::String (processed.load()));
}

// These checks exercise transitions, not just settled impulse responses.
static void runRegressionTests()
{
    juce::MidiBuffer midi;
    for (int mode = 0; mode < 3; ++mode)
    {
        auto render = [&midi, mode] (int size)
        {
            ShellSpaceProcessor p;
            if (mode == 0) configureBodyOnly (p);
            else configureSpaceOnly (p, mode == 2);
            p.prepareToPlay (48000, size);
            std::vector<float> samples;
            juce::AudioBuffer<float> b (2, size);
            for (int offset = 0; offset < 147456; offset += size)
            {
                b.clear();
                if (offset == 0) { b.setSample (0, 0, 1); b.setSample (1, 0, 0.3f); }
                p.processBlock (b, midi);
                for (int i = 0; i < size; ++i)
                    for (int ch = 0; ch < 2; ++ch) samples.push_back (b.getSample (ch, i));
            }
            return samples;
        };
        const auto small = render (64), large = render (512);
        float diff = 0, peak = 0;
        for (size_t i = 0; i < small.size(); ++i)
        {
            diff = std::max (diff, std::abs (small[i] - large[i]));
            peak = std::max (peak, std::abs (large[i]));
        }
        // Different FFT partitions reorder float summations. Bound both the
        // absolute error (-100 dBFS) and the error relative to the response peak
        // (0.1%); this is not a bit-identity claim. Cover the full hall tail.
        check (diff < 1e-5f && diff < peak * 0.001f && peak > 0.001f,
               "Partition policies preserve the stereo impulse response mode " + juce::String (mode),
               "diff=" + juce::String (diff, 9));
    }
    {
        ShellSpaceProcessor p;
        Fader fader (p.apvts, "bodyLevel", {});
        fader.slider.setValue (-12, juce::sendNotificationSync);
        fader.value.setText ("abc12", juce::dontSendNotification);
        fader.value.onTextChange();
        check (fader.slider.getValue() == -12, "Malformed dB text does not change gain");
        fader.value.setText ("-6 dB", juce::dontSendNotification);
        fader.value.onTextChange();
        check (fader.slider.getValue() == -6, "Valid dB suffix is accepted");
    }
    auto processSilence = [&midi] (ShellSpaceProcessor& p, int blocks, int size = 64)
    {
        juce::AudioBuffer<float> b (2, size);
        for (int i = 0; i < blocks; ++i) { b.clear(); p.processBlock (b, midi); }
    };
    {
        ShellSpaceProcessor p;
        configureSpaceOnly (p, false);
        p.prepareToPlay (48000, 64);
        const auto immediate = runImpulse (p, 2, 48000, 64);
        warmUp (p, 48000, 64);
        const auto settled = runImpulse (p, 2, 48000, 64);
        check (std::abs (immediate.peak - settled.peak) < 1e-6f,
               "Initial IR is ready for offline rendering",
               juce::String (immediate.peak, 9) + " vs " + juce::String (settled.peak, 9));
    }
    {
        juce::TemporaryFile temp (".wav");
        std::unique_ptr<juce::OutputStream> stream = temp.getFile().createOutputStream();
        juce::WavAudioFormat format;
        auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
            .withSampleRate (48000).withNumChannels (1).withBitsPerSample (24));
        juce::AudioBuffer<float> ir (1, 128);
        ir.clear(); ir.setSample (0, 10, 0.5f);
        check (writer != nullptr && writer->writeFromAudioSampleBuffer (ir, 0, 128),
               "Mono WAV fixture written");
        writer.reset();
        ShellSpaceProcessor p;
        configureSpaceOnly (p, false);
        p.setUserIR (false, temp.getFile());
        p.prepareToPlay (48000, 64);
        warmUp (p, 48000, 64);
        juce::AudioBuffer<float> b (2, 64);
        b.clear(); b.setSample (0, 0, 1); b.setSample (1, 0, 1);
        p.processBlock (b, midi);
        const auto left = b.getMagnitude (0, 0, 64), right = b.getMagnitude (1, 0, 64);
        check (left > 0.01f && std::abs (left - right) < 1e-6f,
               "Mono SPACE IR feeds both stereo channels",
               juce::String (left, 6) + " / " + juce::String (right, 6));
    }
    for (const bool invalid : { false, true })
    {
        juce::TemporaryFile temp (".wav");
        std::unique_ptr<juce::OutputStream> stream = temp.getFile().createOutputStream();
        juce::WavAudioFormat format;
        auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
            .withSampleRate (48000).withNumChannels (1).withBitsPerSample (32)
            .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        const int length = invalid ? 128 : 192000;
        juce::AudioBuffer<float> ir (1, length);
        ir.clear();
        ir.setSample (0, 0, invalid ? std::numeric_limits<float>::quiet_NaN() : 0.5f);
        check (writer != nullptr && writer->writeFromAudioSampleBuffer (ir, 0, length),
               "Float WAV fixture written");
        writer.reset();
        ShellSpaceProcessor p;
        configureBodyOnly (p);
        setNorm (p, "bodyTune", 0);
        p.setUserIR (true, temp.getFile());
        p.prepareToPlay (48000, 64);
        if (invalid)
            check (p.getIRError (true).isNotEmpty(), "Non-finite external IR is rejected");
        else
            check (p.getTailLengthSeconds() >= 8.0, "Host tail covers stretched external IR");
    }
    auto renderGain = [&midi, &processSilence] (int size, const char* id)
    {
        ShellSpaceProcessor p;
        p.prepareToPlay (48000, size);
        processSilence (p, 2048 / size, size);
        setNorm (p, id, 0.0f);
        std::vector<float> result;
        juce::AudioBuffer<float> b (2, size);
        for (int offset = 0; offset < 2048; offset += size)
        {
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill (b.getWritePointer (ch), 0.1f, size);
            p.processBlock (b, midi);
            for (int i = 0; i < size; ++i) result.push_back (b.getSample (0, i));
        }
        return result;
    };
    {
        ShellSpaceProcessor p;
        configureSpaceOnly (p, true);
        p.prepareToPlay (48000, 64);
        juce::AudioBuffer<float> b (2, 64);
        b.clear(); b.setSample (0, 0, 1); b.setSample (1, 0, 1);
        p.processBlock (b, midi);
        setNorm (p, "bypass", 1);
        processSilence (p, 3000);
        setNorm (p, "bypass", 0);
        float peak = 0;
        for (int i = 0; i < 200; ++i)
        {
            b.clear(); p.processBlock (b, midi);
            peak = std::max (peak, scan (b).peak);
        }
        check (peak < 1e-6f, "Bypass does not replay a frozen tail",
               "peak=" + juce::String (peak, 9));
    }
    for (const auto* id : { "dry", "out" })
    {
        const auto a = renderGain (64, id), b = renderGain (512, id);
        float diff = 0, jump = 0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            diff = std::max (diff, std::abs (a[i] - b[i]));
            if (i > 0) jump = std::max (jump, std::abs (a[i] - a[i-1]));
        }
        check (diff < 1e-6f, juce::String (id) + " ramp is block-size independent",
               "diff=" + juce::String (diff, 9));
        check (jump < 0.0002f, juce::String (id) + " ramp has no boundary jumps",
               "jump=" + juce::String (jump, 9));
    }
    {
        ShellSpaceProcessor p;
        p.prepareToPlay (48000, 64);
        juce::AudioBuffer<float> b (2, 64);
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (b.getWritePointer (ch), 0.1f, 64);
        p.processBlock (b, midi);
        check (std::abs (b.getSample (0, 0) - 0.1f) < 1e-7f,
               "Init is transparent from the first sample",
               "sample=" + juce::String (b.getSample (0, 0), 9));
    }
    for (int mode = 0; mode < 3; ++mode)
    {
        ShellSpaceProcessor p;
        if (mode == 0) configureBodyOnly (p);
        else configureSpaceOnly (p, mode == 2);
        p.prepareToPlay (48000, 64);
        warmUp (p, 48000, 64);
        juce::AudioBuffer<float> b (2, 64);
        b.clear(); b.setSample (0, 0, 1); b.setSample (1, 0, 1);
        p.processBlock (b, midi);
        setNorm (p, mode == 0 ? "bodyMute" : "spaceMute", 1);
        processSilence (p, 3000); // Four seconds: longer than all built-in tails.
        setNorm (p, mode == 0 ? "bodyMute" : "spaceMute", 0);
        float peak = 0;
        for (int i = 0; i < 200; ++i)
        {
            b.clear(); p.processBlock (b, midi);
            peak = std::max (peak, scan (b).peak);
        }
        check (peak < 1e-6f, "Unmute does not replay a frozen tail mode " + juce::String (mode),
               "peak=" + juce::String (peak, 9));
    }
    {
        ShellSpaceProcessor original, restored;
        original.setCurrentProgram (5);
        setNorm (original, "out", 0.63f);
        juce::MemoryBlock state;
        original.getStateInformation (state);
        restored.setStateInformation (state.getData(), (int) state.getSize());
        check (restored.getCurrentProgram() == 5, "Selected program survives state restore");
        check (std::abs (restored.apvts.getParameter ("out")->getValue()
                        - original.apvts.getParameter ("out")->getValue()) < 1e-6f,
               "Edited parameter survives state restore");
        juce::XmlElement foreign ("NOT_SHELLSPACE");
        juce::AudioProcessor::copyXmlToBinary (foreign, state);
        restored.setStateInformation (state.getData(), (int) state.getSize());
        check (restored.apvts.state.hasType ("STATE"), "Foreign state XML is ignored");
    }
    {
        ShellSpaceProcessor p;
        auto tree = p.apvts.copyState();
        auto missing = juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getNonexistentChildFile ("shellspace-missing", ".wav");
        tree.setProperty ("userBodyIR", missing.getFullPathName(), nullptr);
        juce::MemoryBlock state;
        juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), state);
        p.setStateInformation (state.getData(), (int) state.getSize());
        p.prepareToPlay (48000, 64);
        check (p.getIRError (true).isNotEmpty(), "Missing saved IR reports fallback");
    }
}
