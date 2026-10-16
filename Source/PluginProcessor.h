#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include "DSP/ForgeDSP.h"
#include "PresetManager.h"

class ForgeProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    ForgeProcessor();
    ~ForgeProcessor() override { cancelPendingUpdate(); }

    void prepareToPlay (double sampleRate, int maxBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "PTZR.FORGE"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets;

    // GUI read-outs (lock-free)
    std::atomic<float> inPeak { 0.f }, outPeak { 0.f };
    std::atomic<bool> hostPlaying { false };

    // MIDI learn
    std::atomic<int> learnParam { -1 };
    std::array<std::atomic<int>, 128> ccMap;
    int ccForParam (int paramIndex) const { for (int c = 0; c < 128; ++c) if (ccMap[(size_t) c].load() == paramIndex) return c; return -1; }
    void clearMapping (int paramIndex) { for (auto& m : ccMap) if (m.load() == paramIndex) m.store (-1); }

private:
    static constexpr int kSub = 32;                      // internal block size: coefficients/smoothing update every 32 samples
    void handleAsyncUpdate() override { const int p = pendingProgram.exchange (-1); if (p >= 0) presets.apply (p); }
    void handleMidi (juce::MidiBuffer&);
    void processSub (juce::AudioBuffer<float>&, int pos, int n, int inCh, int outCh, float& inPk, float& outPk);

    std::array<juce::RangedAudioParameter*, ids::numParams> params {};
    std::array<std::atomic<float>*, ids::numParams> raw {};
    std::atomic<int> pendingProgram { -1 };

    double fs = 44100.0; int wowCentre = 144; int latency = 0;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    float osRate = 44100.f;

    forge::Biquad emph[2], deemph[2], preLow[2], preHigh[2], postLow[2], postHigh[2], bump[2], lpf[2];
    forge::DcBlocker dcb[2];
    forge::DynCoeffs dyn; forge::DynState dynState[2][4];
    forge::TapeModulator mod[2]; forge::ModDelay wowDelay[2];
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay { 16384 };
    juce::SmoothedValue<float> sDrive, sTone, sWow, sChar, sIn, sOut, sMix, sAuto;
    forge::Model active = forge::Model::Tube, fadeFrom = forge::Model::Tube; int fadeLeft = 0, fadeTotal = 1;
    float lastPreGain = 1.f;
    juce::AudioBuffer<float> work { 2, kSub }, dryBuf { 2, kSub };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForgeProcessor)
};
