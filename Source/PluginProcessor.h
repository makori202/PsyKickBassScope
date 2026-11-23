#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <array>
#include <cmath>

class PsyKickBassScopeProcessor : public juce::AudioProcessor {
public:
    PsyKickBassScopeProcessor();
    ~PsyKickBassScopeProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    void setParamsFromJson(const juce::String& jsonStr);
    juce::String getScopeJson();

private:
    std::atomic<float> pPhaseDeg{0.0f}, pInvert{0.0f}, pSubDuck{0.88f}, pTopDuck{0.65f};
    std::atomic<float> pHoldMs{18.0f}, pRelMs{65.0f}, pCurve{0.60f}, pXoverHz{120.0f};
    std::atomic<float> pWarmth{0.25f}, pMono{1.0f}, pSubGainDb{0.0f}, pM30x{0.0f};

    double sr = 44100.0;
    double lp1L=0, lp2L=0, lp3L=0, lp4L=0, lp1R=0, lp2R=0, lp3R=0, lp4R=0;
    double ap1xL=0, ap1yL=0, ap2xL=0, ap2yL=0, ap1xR=0, ap1yR=0, ap2xR=0, ap2yR=0;
    double beatPhase = 0.0, subEnv = 1.0, topEnv = 1.0;

    static constexpr int SCOPE_SIZE = 128;
    std::array<float, SCOPE_SIZE> scopeWave{};
    std::array<float, SCOPE_SIZE> scopeEnv{};
    std::atomic<int> scopeIdx{0};
    int decimateCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PsyKickBassScopeProcessor)
};
