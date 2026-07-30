#include "PluginEditor.h"

static const juce::Colour kBg     (0xff0b0b10);
static const juce::Colour kPanel  (0xff15151d);
static const juce::Colour kPanel2 (0xff1b1b25);
static const juce::Colour kLine   (0xff26263a);
static const juce::Colour kViolet (0xff8b5cf6);
static const juce::Colour kCyan   (0xff22d3ee);
static const juce::Colour kAmber  (0xfff59e0b);
static const juce::Colour kText   (0xffe9e9f1);
static const juce::Colour kDim    (0xff8a8a9c);

// ═════════════════════════════════════════════════════════════════════════
//  AnalyserComp
// ═════════════════════════════════════════════════════════════════════════
AnalyserComp::AnalyserComp (UncertainMasterProcessor& p) : proc (p)
{
    setInterceptsMouseClicks (true, false);
    startTimerHz (24);
}

float AnalyserComp::bandX (int i) const
{
    const float f = proc.cleanHz[i].load();
    return fToT (f) * (float) getWidth();
}

void AnalyserComp::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff0d0d13));
    g.fillRoundedRectangle (r, 10.f);
    g.setColour (kLine);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.f, 1.f);

    const float W = r.getWidth(), H = r.getHeight();
    const bool delta = proc.apvts.getRawParameterValue ("delta")->load() > 0.5f;

    if (delta)
    {
        // Vue DELTA : ce que le CLEAN retire, en ambre
        juce::Path p; p.startNewSubPath (0, H * 0.82f);
        for (int b = 0; b < UncertainMasterProcessor::kNumCleanBands; ++b)
        {
            const float x  = bandX (b);
            const float dB = proc.cleanDb[b].load();
            p.lineTo (x, H * 0.82f - dB * 26.f);
        }
        p.lineTo (W, H * 0.82f);
        g.setColour (kAmber.withAlpha (0.25f)); g.fillPath (p);
        g.setColour (kAmber); g.strokePath (p, juce::PathStrokeType (2.f));
        g.setColour (kAmber.withAlpha (0.9f));
        g.setFont (juce::FontOptions (12.f, juce::Font::bold));
        g.drawText ("DELTA - CE QUE LE CLEAN RETIRE", 10, 8, (int) W - 20, 16,
                    juce::Justification::left);
    }
    else
    {
        // Courbe cible "master rap" en pointille
        juce::Path tgt;
        for (int px = 0; px <= (int) W; px += 6)
        {
            const float t = px / W;
            float y = H*0.42f - std::sin (t*juce::MathConstants<float>::pi)*H*0.06f;
            if (t < 0.15f) y += (0.15f - t) * H * 0.9f;
            if (t > 0.85f) y += (t - 0.85f) * H * 0.5f;
            if (px == 0) tgt.startNewSubPath (px, y); else tgt.lineTo (px, y);
        }
        juce::Path dash;
        const float dl[] = { 4.f, 5.f };
        juce::PathStrokeType (1.5f).createDashedStroke (dash, tgt, dl, 2);
        g.setColour (kDim.withAlpha (0.5f)); g.fillPath (dash);

        // Spectre live
        juce::Path sp;
        const int N = UncertainMasterProcessor::kNumScopeBins;
        for (int b = 0; b < N; ++b)
        {
            const float t = (float) b / (N - 1);
            const float v = proc.scope[b].load();
            const float y = H - v * H * 0.9f - H * 0.05f;
            if (b == 0) sp.startNewSubPath (t * W, y); else sp.lineTo (t * W, y);
        }
        juce::ColourGradient grad (kViolet, 0, 0, kCyan, W, 0, false);
        g.setGradientFill (grad);
        g.strokePath (sp, juce::PathStrokeType (2.5f));
    }

    // Points CLEAN deplacables (toujours visibles)
    for (int b = 0; b < UncertainMasterProcessor::kNumCleanBands; ++b)
    {
        const float x  = bandX (b);
        const float dB = proc.cleanDb[b].load();
        const float act = juce::jlimit (0.f, 1.f, dB / 6.f);
        const bool grab = (b == dragBand);

        if (grab)
        {
            g.setColour (kCyan.withAlpha (0.35f));
            g.drawVerticalLine ((int) x, 0.f, H);
        }
        g.setColour (grab ? juce::Colours::white : kCyan.withAlpha (0.35f + act * 0.65f));
        g.fillEllipse (x - (grab ? 7.f : 5.f), H - 22.f, grab ? 14.f : 10.f, grab ? 14.f : 10.f);
        g.setColour (kCyan.withAlpha (0.6f));
        g.drawEllipse (x - 11.f, H - 26.f, 22.f, 22.f, 1.5f);

        g.setColour (kText.withAlpha (0.6f));
        g.setFont (juce::FontOptions (11.f, juce::Font::bold));
        g.drawText (dB > 0.2f ? "-" + juce::String (dB, 1) : "0",
                    (int) x - 24, (int) H - 46, 48, 12, juce::Justification::centred);

        const float f = proc.cleanHz[b].load();
        g.setColour (kDim);
        g.setFont (juce::FontOptions (10.f));
        g.drawText (f >= 1000.f ? juce::String (f/1000.f, 1) + "k" : juce::String ((int) f),
                    (int) x - 24, (int) H - 14, 48, 12, juce::Justification::centred);
    }
}

void AnalyserComp::mouseDown (const juce::MouseEvent& e)
{
    for (int b = 0; b < UncertainMasterProcessor::kNumCleanBands; ++b)
        if (std::abs (e.x - bandX (b)) < 18.f) { dragBand = b; break; }
}

void AnalyserComp::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0) return;
    const char* czId[4] = { "cz0", "cz1", "cz2", "cz3" };
    const float t = juce::jlimit (0.f, 1.f, (float) e.x / (float) getWidth());
    const float f = tToF (t);
    if (auto* p = proc.apvts.getParameter (czId[dragBand]))
        p->setValueNotifyingHost (p->convertTo0to1 (f));
}

void AnalyserComp::mouseUp (const juce::MouseEvent&) { dragBand = -1; }

// ═════════════════════════════════════════════════════════════════════════
//  EqBandSlider
// ═════════════════════════════════════════════════════════════════════════
EqBandSlider::EqBandSlider (UncertainMasterProcessor& p, const juce::String& paramId,
                            const juce::String& name, const juce::String& freq)
{
    slider.setSliderStyle (juce::Slider::LinearVertical);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 44, 16);
    slider.setColour (juce::Slider::trackColourId, kViolet.withAlpha (0.5f));
    slider.setColour (juce::Slider::thumbColourId, kCyan);
    slider.setColour (juce::Slider::textBoxTextColourId, kText);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (slider);

    nameL.setText (name, juce::dontSendNotification);
    nameL.setJustificationType (juce::Justification::centred);
    nameL.setColour (juce::Label::textColourId, kDim);
    nameL.setFont (juce::FontOptions (8.f, juce::Font::bold));
    addAndMakeVisible (nameL);

    freqL.setText (freq, juce::dontSendNotification);
    freqL.setJustificationType (juce::Justification::centred);
    freqL.setColour (juce::Label::textColourId, kDim);
    freqL.setFont (juce::FontOptions (8.f));
    addAndMakeVisible (freqL);

    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            p.apvts, paramId, slider);
}

void EqBandSlider::paint (juce::Graphics& g)
{
    g.setColour (kPanel2);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.f);
}

void EqBandSlider::resized()
{
    auto r = getLocalBounds().reduced (4);
    nameL.setBounds (r.removeFromTop (12));
    freqL.setBounds (r.removeFromBottom (12));
    slider.setBounds (r);
}

// ═════════════════════════════════════════════════════════════════════════
//  UncertainMasterEditor
// ═════════════════════════════════════════════════════════════════════════
UncertainMasterEditor::UncertainMasterEditor (UncertainMasterProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    title.setText ("UNCERTAIN MASTER", juce::dontSendNotification);
    title.setJustificationType (juce::Justification::centredLeft);
    title.setColour (juce::Label::textColourId, kText);
    title.setFont (juce::FontOptions (18.f, juce::Font::bold));
    addAndMakeVisible (title);

    for (auto* b : { &mSimple, &mAdv })
    {
        b->setColour (juce::TextButton::buttonColourId, kPanel);
        b->setColour (juce::TextButton::buttonOnColourId, kViolet);
        b->setColour (juce::TextButton::textColourOffId, kDim);
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        b->setClickingTogglesState (false);
        addAndMakeVisible (*b);
    }
    mSimple.onClick = [this] { setMode (false); };
    mAdv.onClick    = [this] { setMode (true); };

    styleKnob (clean,  cleanL, "CLEAN");
    styleKnob (boostK, boostL, "BOOST");
    styleKnob (driveK, driveL, "DRIVE");
    styleKnob (voiceK, voiceL, "VOICE");

    outputS.setSliderStyle (juce::Slider::LinearHorizontal);
    outputS.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 18);
    outputS.setColour (juce::Slider::trackColourId, kViolet.withAlpha (0.6f));
    outputS.setColour (juce::Slider::thumbColourId, kCyan);
    outputS.setColour (juce::Slider::textBoxTextColourId, kText);
    outputS.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (outputS);
    outL.setText ("OUTPUT", juce::dontSendNotification);
    outL.setColour (juce::Label::textColourId, kDim);
    outL.setFont (juce::FontOptions (11.f, juce::Font::bold));
    addAndMakeVisible (outL);

    // Presets (exclusifs, pilotent le param "preset")
    auto setupPreset = [this] (juce::TextButton& b, int idx)
    {
        b.setColour (juce::TextButton::buttonColourId, kPanel);
        b.setColour (juce::TextButton::textColourOffId, kDim);
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        b.setClickingTogglesState (false);
        b.onClick = [this, idx] {
            if (auto* pr = proc.apvts.getParameter ("preset"))
                pr->setValueNotifyingHost (pr->convertTo0to1 ((float) idx));
        };
        addAndMakeVisible (b);
    };
    setupPreset (pOff, 0); setupPreset (pWarm, 1);
    setupPreset (pAir, 2); setupPreset (pImpact, 3);

    // Chips BOOST cumulables (toggles -> params bool)
    for (auto* c : { &chipLow, &chipMid, &chipAir })
    {
        c->setColour (juce::TextButton::buttonColourId, kPanel);
        c->setColour (juce::TextButton::buttonOnColourId, kCyan);
        c->setColour (juce::TextButton::textColourOffId, kDim);
        c->setColour (juce::TextButton::textColourOnId, juce::Colour (0xff0b0b10));
        c->setClickingTogglesState (true);
        addAndMakeVisible (*c);
    }

    // Clip caractere (exclusif -> param choice)
    auto setupClip = [this] (juce::TextButton& b, int idx)
    {
        b.setColour (juce::TextButton::buttonColourId, kPanel2);
        b.setColour (juce::TextButton::textColourOffId, kDim);
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        b.setClickingTogglesState (false);
        b.onClick = [this, idx] {
            if (auto* pr = proc.apvts.getParameter ("clipchar"))
                pr->setValueNotifyingHost (pr->convertTo0to1 ((float) idx));
        };
        addAndMakeVisible (b);
    };
    setupClip (clipSoft, 0); setupClip (clipPunch, 1); setupClip (clipHard, 2);

    deltaBtn.setColour (juce::TextButton::buttonColourId, kPanel);
    deltaBtn.setColour (juce::TextButton::buttonOnColourId, kAmber);
    deltaBtn.setColour (juce::TextButton::textColourOffId, kDim);
    deltaBtn.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff0b0b10));
    deltaBtn.setClickingTogglesState (true);
    addAndMakeVisible (deltaBtn);

    advTitle.setText ("CLEAN ADAPTATIF - GLISSE LES POINTS", juce::dontSendNotification);
    advTitle.setColour (juce::Label::textColourId, kDim);
    advTitle.setFont (juce::FontOptions (9.f, juce::Font::bold));
    addAndMakeVisible (advTitle);

    eqTitle.setText ("BOOST DETAILLE - 5 BANDES (+/-6 dB)", juce::dontSendNotification);
    eqTitle.setColour (juce::Label::textColourId, kDim);
    eqTitle.setFont (juce::FontOptions (9.f, juce::Font::bold));
    addAndMakeVisible (eqTitle);

    voiceTitle.setText ("VOICE - CLARTE ADAPTATIVE MID/SIDE (voix centre / beat cotes)",
                        juce::dontSendNotification);
    voiceTitle.setColour (juce::Label::textColourId, kDim);
    voiceTitle.setFont (juce::FontOptions (9.f, juce::Font::bold));
    addAndMakeVisible (voiceTitle);

    auto styleMini = [this] (juce::Slider& s, juce::Label& l, const juce::String& name)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 16);
        s.setColour (juce::Slider::rotarySliderFillColourId, kCyan);
        s.setColour (juce::Slider::rotarySliderOutlineColourId, kPanel.brighter (0.15f));
        s.setColour (juce::Slider::thumbColourId, kViolet);
        s.setColour (juce::Slider::textBoxTextColourId, kText);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (s);
        l.setText (name, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::textColourId, kDim);
        l.setFont (juce::FontOptions (8.f, juce::Font::bold));
        addAndMakeVisible (l);
    };
    styleMini (vFreq,  vFreqL,  "FREQ");
    styleMini (vCarve, vCarveL, "CARVE");
    styleMini (vDuck,  vDuckL,  "DUCK BEAT");
    styleMini (vLift,  vLiftL,  "LIFT VOIX");

    analyser = std::make_unique<AnalyserComp> (proc);
    addAndMakeVisible (*analyser);

    const char* eqId[5]   = { "eqLow", "eqLowMid", "eqMid", "eqHighMid", "eqAir" };
    const char* eqName[5] = { "LOW", "LOW MID", "MID", "HIGH MID", "AIR" };
    const char* eqFrq[5]  = { "90", "350", "1.2k", "3k", "10k" };
    for (int i = 0; i < 5; ++i)
        eqBands.add (new EqBandSlider (proc, eqId[i], eqName[i], eqFrq[i]));
    for (auto* e : eqBands) addAndMakeVisible (*e);

    aClean = std::make_unique<SA> (proc.apvts, "clean",  clean);
    aBoost = std::make_unique<SA> (proc.apvts, "boost",  boostK);
    aDrive = std::make_unique<SA> (proc.apvts, "drive",  driveK);
    aVoice = std::make_unique<SA> (proc.apvts, "voice",  voiceK);
    aOut   = std::make_unique<SA> (proc.apvts, "output", outputS);
    aVFreq  = std::make_unique<SA> (proc.apvts, "voiceFreq",  vFreq);
    aVCarve = std::make_unique<SA> (proc.apvts, "voiceCarve", vCarve);
    aVDuck  = std::make_unique<SA> (proc.apvts, "voiceDuck",  vDuck);
    aVLift  = std::make_unique<SA> (proc.apvts, "voiceLift",  vLift);
    aChipLow = std::make_unique<BA> (proc.apvts, "chipLow", chipLow);
    aChipMid = std::make_unique<BA> (proc.apvts, "chipMid", chipMid);
    aChipAir = std::make_unique<BA> (proc.apvts, "chipAir", chipAir);
    aDelta   = std::make_unique<BA> (proc.apvts, "delta",   deltaBtn);

    setMode (false);
    startTimerHz (24);
}

void UncertainMasterEditor::styleKnob (juce::Slider& s, juce::Label& l, const juce::String& name)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 18);
    s.setColour (juce::Slider::rotarySliderFillColourId, kViolet);
    s.setColour (juce::Slider::rotarySliderOutlineColourId, kPanel.brighter (0.15f));
    s.setColour (juce::Slider::thumbColourId, kCyan);
    s.setColour (juce::Slider::textBoxTextColourId, kText);
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (s);
    l.setText (name, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setColour (juce::Label::textColourId, kDim);
    l.setFont (juce::FontOptions (12.f, juce::Font::bold));
    addAndMakeVisible (l);
}

void UncertainMasterEditor::setMode (bool adv)
{
    advanced = adv;
    const bool showAdv = adv;
    advTitle.setVisible (showAdv);
    eqTitle.setVisible (showAdv);
    voiceTitle.setVisible (showAdv);
    analyser->setVisible (showAdv);
    for (auto* e : eqBands) e->setVisible (showAdv);
    for (auto* b : { &clipSoft, &clipPunch, &clipHard, &deltaBtn }) b->setVisible (showAdv);
    for (auto* s : { &vFreq, &vCarve, &vDuck, &vLift }) s->setVisible (showAdv);
    for (auto* l : { &vFreqL, &vCarveL, &vDuckL, &vLiftL }) l->setVisible (showAdv);

    setSize (kWSimple, showAdv ? kHAdv : kHSimple);
    resized();
    repaint();
}

void UncertainMasterEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kPanel);
    g.fillRoundedRectangle (getLocalBounds().reduced (8).toFloat(), 12.f);

    // Etat mode
    mSimple.setColour (juce::TextButton::buttonColourId, advanced ? kPanel : kViolet);
    mAdv.setColour    (juce::TextButton::buttonColourId, advanced ? kViolet : kPanel);

    // Preset actif
    const int cur = (int) proc.apvts.getRawParameterValue ("preset")->load();
    juce::TextButton* pb[4] = { &pOff, &pWarm, &pAir, &pImpact };
    for (int i = 0; i < 4; ++i)
        pb[i]->setColour (juce::TextButton::buttonColourId, i == cur ? kViolet : kPanel.brighter (0.08f));

    // Clip actif
    const int cc = (int) proc.apvts.getRawParameterValue ("clipchar")->load();
    juce::TextButton* cb[3] = { &clipSoft, &clipPunch, &clipHard };
    for (int i = 0; i < 3; ++i)
        cb[i]->setColour (juce::TextButton::buttonColourId, i == cc ? kViolet : kPanel2);

    // Meter LUFS
    auto m = meterArea.toFloat();
    g.setColour (juce::Colour (0xff0e0e14));
    g.fillRoundedRectangle (m, 5.f);
    auto dbToY = [&m] (float db) {
        return juce::jmap (juce::jlimit (-30.f, 0.f, db), -30.f, 0.f, m.getBottom(), m.getY());
    };
    g.setColour (kCyan.withAlpha (0.16f));
    g.fillRect (juce::Rectangle<float> (m.getX(), dbToY (-10.f), m.getWidth(),
                                        dbToY (-12.f) - dbToY (-10.f)));
    const float lufs = proc.lufsShort.load();
    const float y = dbToY (lufs);
    const bool inZone = (lufs >= -12.5f && lufs <= -9.5f);
    g.setColour (inZone ? kCyan : kViolet);
    g.fillRoundedRectangle (m.getX()+3.f, y, m.getWidth()-6.f, m.getBottom()-y-2.f, 3.f);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (9.f));
    g.drawText ("-10", (int) m.getX()-24, (int) dbToY (-10.f)-6, 22, 12, juce::Justification::right);
    g.drawText ("-12", (int) m.getX()-24, (int) dbToY (-12.f)-6, 22, 12, juce::Justification::right);
    g.drawText ("LUFS", meterArea.getX()-6, meterArea.getBottom()+3, meterArea.getWidth()+12, 12,
                juce::Justification::centred);

    if (advanced)
    {
        // GR glue + ratio, petit bandeau sous l'analyseur
        g.setColour (kDim);
        g.setFont (juce::FontOptions (9.f));
        const float gr = proc.glueGrDb.load();
        g.drawText ("GLUE GR  -" + juce::String (gr, 1) + " dB  (max 3)",
                    16, kHSimple + 4, 220, 14, juce::Justification::left);

        drawVoiceMeter (g, voiceMeterArea);
    }

#if UM_TRIAL
    g.setColour (kAmber);
    g.setFont (juce::FontOptions (11.f, juce::Font::bold));
    g.drawText ("FREE VERSION - uncertain.fr", getLocalBounds().removeFromBottom (18),
                juce::Justification::centred);
#endif
}

void UncertainMasterEditor::drawVoiceMeter (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (area.isEmpty()) return;
    auto r = area.toFloat();
    g.setColour (juce::Colour (0xff0e0e14));
    g.fillRoundedRectangle (r, 6.f);
    g.setColour (kLine);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.f, 1.f);

    // Barre de presence vocale en haut
    auto pres = r.reduced (8).removeFromTop (14);
    const float pv = proc.voicePresence.load();
    g.setColour (kDim); g.setFont (juce::FontOptions (8.f, juce::Font::bold));
    g.drawText ("VOIX DETECTEE", (int) pres.getX(), (int) pres.getY()-1, 90, 10,
                juce::Justification::left);
    auto pbar = pres.withTrimmedLeft (92);
    g.setColour (juce::Colour (0xff1b1b25)); g.fillRoundedRectangle (pbar, 3.f);
    g.setColour (kCyan);
    g.fillRoundedRectangle (pbar.withWidth (pbar.getWidth() * juce::jlimit (0.f,1.f,pv)), 3.f);

    // 4 zones d'energie (proxy instruments)
    const char* zn[4] = { "SUB", "LOW", "VOIX/PRES", "AIR" };
    const float zv[4] = { proc.zoneSub.load(), proc.zoneLow.load(),
                          proc.zonePres.load(), proc.zoneAir.load() };
    auto zarea = r.reduced (8).withTrimmedTop (22);
    const float rowH = zarea.getHeight() / 4.f;
    g.setFont (juce::FontOptions (8.f));
    for (int i = 0; i < 4; ++i)
    {
        auto row = zarea.removeFromTop (rowH).reduced (0, 2);
        g.setColour (kDim);
        g.drawText (zn[i], (int) row.getX(), (int) row.getY(), 58, (int) row.getHeight(),
                    juce::Justification::centredLeft);
        auto bar = row.withTrimmedLeft (60);
        g.setColour (juce::Colour (0xff1b1b25)); g.fillRoundedRectangle (bar, 2.f);
        g.setColour (i == 2 ? kCyan : kViolet);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.f,1.f,zv[i])), 2.f);
    }
}

void UncertainMasterEditor::resized()
{
    auto r = getLocalBounds().reduced (16);

    auto top = r.removeFromTop (30);
    title.setBounds (top.removeFromLeft (200));
    auto modeBox = top.removeFromRight (150);
    mSimple.setBounds (modeBox.removeFromLeft (75).reduced (2));
    mAdv.setBounds    (modeBox.reduced (2));

    r.removeFromTop (6);
    auto pr = r.removeFromTop (28);
    const int bw = pr.getWidth() / 4;
    pOff.setBounds    (pr.removeFromLeft (bw).reduced (3, 1));
    pWarm.setBounds   (pr.removeFromLeft (bw).reduced (3, 1));
    pAir.setBounds    (pr.removeFromLeft (bw).reduced (3, 1));
    pImpact.setBounds (pr.reduced (3, 1));

    r.removeFromTop (8);

    // Meter a droite (toujours a la meme place)
    auto simpleZone = r;
    auto right = simpleZone.removeFromRight (64);
    meterArea = juce::Rectangle<int> (right.getX()+18, r.getY(), 26, 150);

    // 4 knobs : CLEAN / BOOST / VOICE / DRIVE
    auto k = simpleZone.removeFromTop (150);
    const int kw = k.getWidth() / 4;
    auto place = [] (juce::Rectangle<int> a, juce::Slider& s, juce::Label& l) {
        l.setBounds (a.removeFromTop (16));
        s.setBounds (a.reduced (4));
    };
    auto kClean = k.removeFromLeft (kw);
    auto kBoost = k.removeFromLeft (kw);
    auto kVoice = k.removeFromLeft (kw);
    auto kDrive = k;
    place (kClean, clean, cleanL);
    place (kBoost, boostK, boostL);
    place (kVoice, voiceK, voiceL);
    place (kDrive, driveK, driveL);

    // Chips cumulables sous BOOST
    auto chipRow = juce::Rectangle<int> (kBoost.getX()+2, kBoost.getBottom()+2, kw-4, 22);
    const int cw = chipRow.getWidth() / 3;
    chipLow.setBounds (chipRow.removeFromLeft (cw).reduced (1, 0));
    chipMid.setBounds (chipRow.removeFromLeft (cw).reduced (1, 0));
    chipAir.setBounds (chipRow.reduced (1, 0));

    // Output
    auto out = juce::Rectangle<int> (r.getX(), kBoost.getBottom()+32, simpleZone.getWidth(), 26);
    outL.setBounds (out.removeFromLeft (60));
    outputS.setBounds (out);

    if (! advanced) return;

    // ── Zone ADVANCED ────────────────────────────────────────────────────
    auto a = getLocalBounds().reduced (16).withTop (kHSimple + 20);
    advTitle.setBounds (a.removeFromTop (14));
    a.removeFromTop (4);
    analyser->setBounds (a.removeFromTop (150));
    a.removeFromTop (14);

    // Clip + delta
    auto clipRow = a.removeFromTop (26);
    clipSoft.setBounds  (clipRow.removeFromLeft (70).reduced (2, 0));
    clipPunch.setBounds (clipRow.removeFromLeft (70).reduced (2, 0));
    clipHard.setBounds  (clipRow.removeFromLeft (70).reduced (2, 0));
    deltaBtn.setBounds  (clipRow.removeFromRight (100).reduced (2, 0));
    a.removeFromTop (12);

    eqTitle.setBounds (a.removeFromTop (14));
    a.removeFromTop (4);
    auto eqRow = a.removeFromTop (120);
    const int ew = eqRow.getWidth() / 5;
    for (int i = 0; i < eqBands.size(); ++i)
        eqBands[i]->setBounds (eqRow.removeFromLeft (ew).reduced (4, 0));

    a.removeFromTop (12);
    voiceTitle.setBounds (a.removeFromTop (14));
    a.removeFromTop (4);
    // 4 mini-knobs voice a gauche + jauge presence/zones a droite
    auto vRow = a.removeFromTop (96);
    auto vMeterZone = vRow.removeFromRight (150);
    const int vw = vRow.getWidth() / 4;
    auto placeMini = [] (juce::Rectangle<int> b, juce::Slider& s, juce::Label& l) {
        l.setBounds (b.removeFromTop (12));
        s.setBounds (b.reduced (3));
    };
    placeMini (vRow.removeFromLeft (vw), vFreq,  vFreqL);
    placeMini (vRow.removeFromLeft (vw), vCarve, vCarveL);
    placeMini (vRow.removeFromLeft (vw), vDuck,  vDuckL);
    placeMini (vRow,                     vLift,  vLiftL);
    voiceMeterArea = vMeterZone.reduced (6);
}
