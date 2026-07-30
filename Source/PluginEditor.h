#pragma once
#include "PluginProcessor.h"

// ── Analyseur : spectre live + courbe cible + 4 points CLEAN deplacables ─
class AnalyserComp : public juce::Component,
                     private juce::Timer
{
public:
    explicit AnalyserComp (UncertainMasterProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

private:
    void timerCallback() override { repaint(); }
    static float tToF (float t) { return 20.f * std::pow (1000.f, t); }
    static float fToT (float f) { return std::log (f / 20.f) / std::log (1000.f); }
    float bandX (int i) const;

    UncertainMasterProcessor& proc;
    int dragBand = -1;
};

// ── Mini-slider vertical d'une bande du boost detaille ──────────────────
class EqBandSlider : public juce::Component
{
public:
    EqBandSlider (UncertainMasterProcessor&, const juce::String& paramId,
                  const juce::String& name, const juce::String& freq);
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    juce::Slider slider;
    juce::Label  nameL, freqL;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

// ─────────────────────────────────────────────────────────────────────────
class UncertainMasterEditor : public juce::AudioProcessorEditor,
                              private juce::Timer
{
public:
    explicit UncertainMasterEditor (UncertainMasterProcessor&);
    ~UncertainMasterEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override
    {
        repaint (meterArea);
        if (advanced && ! voiceMeterArea.isEmpty()) repaint (voiceMeterArea);
    }
    void setMode (bool advanced);
    void styleKnob (juce::Slider&, juce::Label&, const juce::String&);

    UncertainMasterProcessor& proc;

    juce::Slider clean, boostK, driveK, voiceK, outputS;
    juce::Label  title, cleanL, boostL, driveL, voiceL, outL, advTitle, eqTitle, voiceTitle;
    juce::Slider vFreq, vCarve, vDuck, vLift;
    juce::Label  vFreqL, vCarveL, vDuckL, vLiftL;

    juce::TextButton mSimple {"SIMPLE"}, mAdv {"ADVANCED"};
    juce::TextButton pOff {"OFF"}, pWarm {"CHALEUR"}, pAir {"AIR"}, pImpact {"IMPACT"};
    juce::TextButton chipLow {"GRAVE"}, chipMid {"MID"}, chipAir {"AIR"};
    juce::TextButton clipSoft {"SOFT"}, clipPunch {"PUNCH"}, clipHard {"HARD"};
    juce::TextButton deltaBtn { juce::CharPointer_UTF8 ("\xce\x94 DELTA") };

    std::unique_ptr<AnalyserComp> analyser;
    juce::OwnedArray<EqBandSlider> eqBands;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SA> aClean, aBoost, aDrive, aVoice, aOut;
    std::unique_ptr<SA> aVFreq, aVCarve, aVDuck, aVLift;
    std::unique_ptr<BA> aChipLow, aChipMid, aChipAir, aDelta;

    void drawVoiceMeter (juce::Graphics&, juce::Rectangle<int>);

    bool advanced = false;
    juce::Rectangle<int> meterArea, voiceMeterArea;

    static constexpr int kWSimple = 600, kHSimple = 370, kHAdv = 880;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UncertainMasterEditor)
};
