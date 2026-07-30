#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <array>

// ─────────────────────────────────────────────────────────────────────────
//  Biquad RBJ
// ─────────────────────────────────────────────────────────────────────────
struct Biquad
{
    float b0=1.f,b1=0.f,b2=0.f,a1=0.f,a2=0.f,z1=0.f,z2=0.f;
    void reset() { z1=z2=0.f; }
    inline float process (float x)
    {
        const float y = b0*x + z1;
        z1 = b1*x - a1*y + z2;
        z2 = b2*x - a2*y;
        return y;
    }
    void setPeaking (double sr, float f, float q, float gDb)
    {
        const float A = std::pow (10.f, gDb/40.f);
        const float w = juce::MathConstants<float>::twoPi * f / (float) sr;
        const float c = std::cos(w), s = std::sin(w);
        const float al = s / (2.f*q);
        const float a0 = 1.f + al/A;
        b0=(1.f+al*A)/a0; b1=(-2.f*c)/a0; b2=(1.f-al*A)/a0;
        a1=(-2.f*c)/a0;   a2=(1.f-al/A)/a0;
    }
    void setLowShelf (double sr, float f, float gDb)
    {
        const float A = std::pow (10.f, gDb/40.f);
        const float w = juce::MathConstants<float>::twoPi * f / (float) sr;
        const float c = std::cos(w), s = std::sin(w);
        const float be = 2.f * std::sqrt(A) * s * 0.7071f;
        const float a0 = (A+1.f)+(A-1.f)*c+be;
        b0=A*((A+1.f)-(A-1.f)*c+be)/a0;
        b1=2.f*A*((A-1.f)-(A+1.f)*c)/a0;
        b2=A*((A+1.f)-(A-1.f)*c-be)/a0;
        a1=-2.f*((A-1.f)+(A+1.f)*c)/a0;
        a2=((A+1.f)+(A-1.f)*c-be)/a0;
    }
    void setHighShelf (double sr, float f, float gDb)
    {
        const float A = std::pow (10.f, gDb/40.f);
        const float w = juce::MathConstants<float>::twoPi * f / (float) sr;
        const float c = std::cos(w), s = std::sin(w);
        const float be = 2.f * std::sqrt(A) * s * 0.7071f;
        const float a0 = (A+1.f)-(A-1.f)*c+be;
        b0=A*((A+1.f)+(A-1.f)*c+be)/a0;
        b1=-2.f*A*((A-1.f)+(A+1.f)*c)/a0;
        b2=A*((A+1.f)+(A-1.f)*c-be)/a0;
        a1=2.f*((A-1.f)-(A+1.f)*c)/a0;
        a2=((A+1.f)-(A-1.f)*c-be)/a0;
    }
    void setHighpass (double sr, float f, float q)
    {
        const float w = juce::MathConstants<float>::twoPi * f / (float) sr;
        const float c = std::cos(w), s = std::sin(w);
        const float al = s/(2.f*q);
        const float a0 = 1.f+al;
        b0=(1.f+c)*0.5f/a0; b1=-(1.f+c)/a0; b2=(1.f+c)*0.5f/a0;
        a1=(-2.f*c)/a0; a2=(1.f-al)/a0;
    }
    void setBandpass (double sr, float f, float q)
    {
        const float w = juce::MathConstants<float>::twoPi * f / (float) sr;
        const float c = std::cos(w), s = std::sin(w);
        const float al = s/(2.f*q);
        const float a0 = 1.f+al;
        b0=al/a0; b1=0.f; b2=-al/a0;
        a1=(-2.f*c)/a0; a2=(1.f-al)/a0;
    }
    void setIdentity() { b0=1.f; b1=b2=a1=a2=0.f; }
};

struct EnvFollower
{
    float v=0.f, aC=0.f, rC=0.f;
    void set (double sr, float attMs, float relMs)
    {
        aC = std::exp (-1.f / (0.001f*attMs*(float) sr));
        rC = std::exp (-1.f / (0.001f*relMs*(float) sr));
        v = 0.f;
    }
    inline float process (float x)
    {
        x = std::fabs (x);
        const bool up = x > v;
        const float c = up ? aC : rC;
        v = c*v + (1.f-c)*x;
        return v;
    }
};

// Une bande CLEAN adaptative (frequence deplacable depuis l'UI)
struct CleanBand
{
    float freq = 1000.f, q = 2.2f, setFreq = -1.f;
    Biquad det;
    EnvFollower fast, slow;
    float cutDb = 0.f;
    Biquad cut[2];

    void prepare (double sr, float f)
    {
        freq = f; setFreq = -1.f;
        det.setBandpass (sr, f, q); det.reset();
        fast.set (sr, 5.f, 160.f);
        slow.set (sr, 1500.f, 1500.f);
        cutDb = 0.f;
        cut[0].setIdentity(); cut[1].setIdentity();
        cut[0].reset(); cut[1].reset();
    }
    void retuneIfNeeded (double sr, float f)
    {
        if (std::fabs (f - setFreq) > 0.5f)
        {
            setFreq = f; freq = f;
            det.setBandpass (sr, f, q);
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────
class UncertainMasterProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kNumCleanBands = 4;
    static constexpr int kNumEqBands    = 5;
    static constexpr int fftOrder       = 11;
    static constexpr int fftSize        = 1 << fftOrder;   // 2048
    static constexpr int kNumScopeBins  = 128;

    UncertainMasterProcessor();
    ~UncertainMasterProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
            && l.getMainInputChannelSet()  == juce::AudioChannelSet::stereo();
    }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& dest) override;
    void setStateInformation (const void* data, int size) override;

    juce::AudioProcessorValueTreeState apvts;

    // ── Meters lus par l'editeur (temps reel, lock-free) ────────────────
    std::atomic<float> lufsShort { -70.f };
    std::atomic<float> glueGrDb  { 0.f };
    std::atomic<float> cleanDb[kNumCleanBands];
    std::atomic<float> cleanHz[kNumCleanBands];
    std::atomic<float> scope[kNumScopeBins];

    // VOICE : detection de presence vocale + energie par zone (instruments)
    std::atomic<float> voicePresence { 0.f };
    std::atomic<float> zoneSub  { 0.f };
    std::atomic<float> zoneLow  { 0.f };
    std::atomic<float> zonePres { 0.f };
    std::atomic<float> zoneAir  { 0.f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void pushToScope (float sample);

    double sr = 44100.0;

    CleanBand bands[kNumCleanBands];

    Biquad eq[kNumEqBands][2];
    float  lastEqDb[kNumEqBands] { -99.f, -99.f, -99.f, -99.f, -99.f };

    Biquad colA[2], colB[2];
    Biquad airGuardDet;
    EnvFollower airGuardEnv;
    int   lastPreset = -1;
    float lastAirDb  = -99.f;

    // VOICE : Mid/Side adaptatif (voix centre, beat cotes)
    Biquad voiceDetBP;          // bandpass presence sur le Mid (detection + lift)
    Biquad sideBP;              // bandpass presence sur le Side (carve)
    EnvFollower voiceFast, voiceSlow;
    float voiceFreqSet = -1.f;
    float presenceSm = 0.f;
    float carveSm = 0.f, liftSm = 0.f, duckSm = 0.f;

    Biquad glueHP;
    EnvFollower glueFast, glueSlow, glueRms;
    float glueGain = 1.f;
    int   glueHold = 0;

    juce::dsp::LinkwitzRileyFilter<float> lrLow, lrHigh;
    juce::dsp::DelayLine<float> subDelay { 8192 };
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    juce::AudioBuffer<float> lowBuf, highBuf;
    EnvFollower crestPeak, crestRms;

    float tpGain = 1.f;
    juce::SmoothedValue<float> outSm;

    Biquad kHP, kShelf;
    float lufsMs = 0.f, lufsCoef = 0.f;

    // Analyseur FFT
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> win { (size_t) fftSize,
                                              juce::dsp::WindowingFunction<float>::hann };
    std::array<float, fftSize>     fifo {};
    std::array<float, fftSize * 2> fftBuf {};
    int fifoIdx = 0;
    std::array<float, kNumScopeBins> scopeSmooth {};

#if UM_TRIAL
    int   trialCtr  = 0;
    float trialGain = 1.f;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UncertainMasterProcessor)
};
