#include "PluginProcessor.h"
#include "PluginEditor.h"

// Frequences par defaut / bornes des 4 bandes CLEAN (boue / boxy / agression / sifflantes)
static const float kCleanDefault[4] = { 250.f, 1200.f, 3200.f, 7000.f };
static const float kCleanMin[4]     = { 120.f,  700.f, 2200.f, 5000.f };
static const float kCleanMax[4]     = { 600.f, 2000.f, 4500.f, 9000.f };

// Boost 5 bandes : LOW / LOW MID / MID / HIGH MID / AIR
static const float kEqFreq[5] = { 90.f, 350.f, 1200.f, 3000.f, 10000.f };
enum EqType { LowShelf, Bell, HighShelf };
static const int   kEqType[5] = { LowShelf, Bell, Bell, Bell, HighShelf };

// ─────────────────────────────────────────────────────────────────────────
UncertainMasterProcessor::UncertainMasterProcessor()
    : AudioProcessor (BusesProperties()
        .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int b = 0; b < kNumCleanBands; ++b)
    {
        cleanDb[b].store (0.f);
        cleanHz[b].store (kCleanDefault[b]);
    }
    for (int i = 0; i < kNumScopeBins; ++i) scope[i].store (0.f);
}

juce::AudioProcessorValueTreeState::ParameterLayout UncertainMasterProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using C = juce::AudioParameterChoice;
    using B = juce::AudioParameterBool;
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    // Facade (plages elargies au maximum utile)
    l.add (std::make_unique<F>(juce::ParameterID{"clean",1}, "Clean",
            juce::NormalisableRange<float>(0.f,100.f,0.1f), 0.f));
    l.add (std::make_unique<F>(juce::ParameterID{"boost",1}, "Boost",
            juce::NormalisableRange<float>(0.f,100.f,0.1f), 0.f));
    l.add (std::make_unique<F>(juce::ParameterID{"drive",1}, "Drive",
            juce::NormalisableRange<float>(0.f,100.f,0.1f), 0.f));
    l.add (std::make_unique<F>(juce::ParameterID{"voice",1}, "Voice",
            juce::NormalisableRange<float>(0.f,100.f,0.1f), 0.f));
    l.add (std::make_unique<F>(juce::ParameterID{"output",1}, "Output",
            juce::NormalisableRange<float>(-18.f,6.f,0.1f), 0.f));
    l.add (std::make_unique<C>(juce::ParameterID{"preset",1}, "Color",
            juce::StringArray{"OFF","CHALEUR","AIR","IMPACT"}, 0));

    // Chips BOOST cumulables (facade) -> reliees aux bandes EQ LOW/MID/AIR
    l.add (std::make_unique<B>(juce::ParameterID{"chipLow",1}, "Boost Low",  false));
    l.add (std::make_unique<B>(juce::ParameterID{"chipMid",1}, "Boost Mid",  false));
    l.add (std::make_unique<B>(juce::ParameterID{"chipAir",1}, "Boost Air",  false));

    // Boost detaille 5 bandes (advanced) : +/-6 dB (plage elargie)
    const char* eqName[5] = { "EQ Low", "EQ LowMid", "EQ Mid", "EQ HighMid", "EQ Air" };
    const char* eqId[5]   = { "eqLow", "eqLowMid", "eqMid", "eqHighMid", "eqAir" };
    for (int i = 0; i < 5; ++i)
        l.add (std::make_unique<F>(juce::ParameterID{eqId[i],1}, eqName[i],
                juce::NormalisableRange<float>(-6.f,6.f,0.1f), 0.f));

    // VOICE avance : frequence de presence + dosage carve / duck / lift
    l.add (std::make_unique<F>(juce::ParameterID{"voiceFreq",1}, "Voice Freq",
            juce::NormalisableRange<float>(1500.f,5000.f,1.f,0.5f), 2600.f));
    l.add (std::make_unique<F>(juce::ParameterID{"voiceCarve",1}, "Voice Carve",
            juce::NormalisableRange<float>(0.f,6.f,0.1f), 3.5f));
    l.add (std::make_unique<F>(juce::ParameterID{"voiceDuck",1}, "Voice Duck",
            juce::NormalisableRange<float>(0.f,3.f,0.1f), 1.2f));
    l.add (std::make_unique<F>(juce::ParameterID{"voiceLift",1}, "Voice Lift",
            juce::NormalisableRange<float>(0.f,4.f,0.1f), 1.5f));

    // Frequences deplacables des bandes CLEAN (advanced)
    const char* czId[4] = { "cz0", "cz1", "cz2", "cz3" };
    for (int i = 0; i < 4; ++i)
        l.add (std::make_unique<F>(juce::ParameterID{czId[i],1},
                juce::String("Clean Freq ") + juce::String(i+1),
                juce::NormalisableRange<float>(kCleanMin[i], kCleanMax[i], 1.f, 0.4f),
                kCleanDefault[i]));

    // Caractere du clip + delta
    l.add (std::make_unique<C>(juce::ParameterID{"clipchar",1}, "Clip Char",
            juce::StringArray{"SOFT","PUNCH","HARD"}, 0));
    l.add (std::make_unique<B>(juce::ParameterID{"delta",1}, "Delta", false));

    return l;
}

// ─────────────────────────────────────────────────────────────────────────
void UncertainMasterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;

    for (int b = 0; b < kNumCleanBands; ++b) bands[b].prepare (sr, kCleanDefault[b]);

    for (int i = 0; i < kNumEqBands; ++i)
    {
        eq[i][0].setIdentity(); eq[i][0].reset();
        eq[i][1].setIdentity(); eq[i][1].reset();
        lastEqDb[i] = -99.f;
    }

    for (int c = 0; c < 2; ++c)
    {
        colA[c].setIdentity(); colA[c].reset();
        colB[c].setIdentity(); colB[c].reset();
    }
    lastPreset = -1; lastAirDb = -99.f;

    airGuardDet.setBandpass (sr, 7000.f, 1.2f); airGuardDet.reset();
    airGuardEnv.set (sr, 10.f, 300.f);

    // VOICE : detection presence + carve/lift
    voiceFreqSet = -1.f;
    voiceDetBP.setBandpass (sr, 2600.f, 1.3f); voiceDetBP.reset();
    sideBP.setBandpass    (sr, 2600.f, 1.3f);  sideBP.reset();
    voiceFast.set (sr, 6.f, 140.f);
    voiceSlow.set (sr, 1400.f, 1400.f);
    presenceSm = carveSm = liftSm = duckSm = 0.f;

    glueHP.setHighpass (sr, 150.f, 0.707f); glueHP.reset();
    glueFast.set (sr, 10.f,  80.f);
    glueSlow.set (sr, 60.f, 300.f);
    glueRms .set (sr, 800.f, 800.f);
    glueGain = 1.f; glueHold = 0;

    juce::dsp::ProcessSpec spec { sr, (juce::uint32) samplesPerBlock, 2 };
    lrLow .prepare (spec); lrLow .setType (juce::dsp::LinkwitzRileyFilterType::lowpass);  lrLow .setCutoffFrequency (120.f);
    lrHigh.prepare (spec); lrHigh.setType (juce::dsp::LinkwitzRileyFilterType::highpass); lrHigh.setCutoffFrequency (120.f);

    os = std::make_unique<juce::dsp::Oversampling<float>> (
            2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    os->initProcessing ((size_t) samplesPerBlock);
    const int lat = (int) std::lround (os->getLatencyInSamples());

    subDelay.prepare (spec);
    subDelay.setMaximumDelayInSamples (juce::jmax (16, lat + 8));
    subDelay.setDelay ((float) lat);
    setLatencySamples (lat);

    lowBuf .setSize (2, samplesPerBlock);
    highBuf.setSize (2, samplesPerBlock);

    crestPeak.set (sr, 0.3f, 300.f);
    crestRms .set (sr, 60.f, 300.f);

    tpGain = 1.f;
    outSm.reset (sr, 0.05);
    outSm.setCurrentAndTargetValue (1.f);

    kHP   .setHighpass  (sr, 60.f, 0.707f); kHP.reset();
    kShelf.setHighShelf (sr, 1680.f, 4.f);  kShelf.reset();
    lufsMs = 0.f;
    lufsCoef = 1.f - std::exp (-1.f / (3.f * (float) sr));

    fifo.fill (0.f); fftBuf.fill (0.f); scopeSmooth.fill (0.f);
    fifoIdx = 0;

#if UM_TRIAL
    trialCtr = 0; trialGain = 1.f;
#endif
}

// ─────────────────────────────────────────────────────────────────────────
void UncertainMasterProcessor::pushToScope (float s)
{
    fifo[(size_t) fifoIdx] = s;
    if (++fifoIdx >= fftSize)
    {
        fifoIdx = 0;
        std::copy (fifo.begin(), fifo.end(), fftBuf.begin());
        std::fill (fftBuf.begin() + fftSize, fftBuf.end(), 0.f);
        win.multiplyWithWindowingTable (fftBuf.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (fftBuf.data());

        // Regroupement log 20 Hz -> 20 kHz sur kNumScopeBins
        for (int b = 0; b < kNumScopeBins; ++b)
        {
            const float t0 = (float) b       / kNumScopeBins;
            const float t1 = (float)(b + 1)  / kNumScopeBins;
            const float f0 = 20.f * std::pow (1000.f, t0);
            const float f1 = 20.f * std::pow (1000.f, t1);
            int k0 = juce::jlimit (1, fftSize/2 - 1, (int) (f0 / (float) sr * fftSize));
            int k1 = juce::jlimit (k0 + 1, fftSize/2, (int) (f1 / (float) sr * fftSize));
            float mx = 0.f;
            for (int k = k0; k < k1; ++k) mx = juce::jmax (mx, fftBuf[(size_t) k]);
            const float db = juce::Decibels::gainToDecibels (mx / (float) fftSize + 1.0e-9f);
            const float norm = juce::jlimit (0.f, 1.f, (db + 90.f) / 90.f);
            scopeSmooth[(size_t) b] += 0.4f * (norm - scopeSmooth[(size_t) b]);
            scope[(size_t) b].store (scopeSmooth[(size_t) b]);
        }

        // Energie par zone (proxy "instruments") : sub / low / presence / air
        auto zoneAvg = [this] (float fa, float fb)
        {
            float acc = 0.f; int cnt = 0;
            for (int b = 0; b < kNumScopeBins; ++b)
            {
                const float f = 20.f * std::pow (1000.f, (float) b / kNumScopeBins);
                if (f >= fa && f < fb) { acc += scopeSmooth[(size_t) b]; ++cnt; }
            }
            return cnt > 0 ? acc / (float) cnt : 0.f;
        };
        zoneSub .store (zoneAvg (20.f,   80.f));
        zoneLow .store (zoneAvg (80.f,  400.f));
        zonePres.store (zoneAvg (1500.f, 5000.f));
        zoneAir .store (zoneAvg (8000.f, 20000.f));
    }
}

// ─────────────────────────────────────────────────────────────────────────
void UncertainMasterProcessor::processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = buf.getNumSamples();
    if (n == 0) return;
    lowBuf .setSize (2, n, false, false, true);
    highBuf.setSize (2, n, false, false, true);

    const float clean  = apvts.getRawParameterValue("clean")->load()  * 0.01f;
    const float boost  = apvts.getRawParameterValue("boost")->load()  * 0.01f;
    const float drive  = apvts.getRawParameterValue("drive")->load()  * 0.01f;
    const float voice  = apvts.getRawParameterValue("voice")->load()  * 0.01f;
    const float outDb  = apvts.getRawParameterValue("output")->load();
    const int   preset = (int) apvts.getRawParameterValue("preset")->load();
    const int   clipCh = (int) apvts.getRawParameterValue("clipchar")->load();
    const bool  delta  = apvts.getRawParameterValue("delta")->load() > 0.5f;

    const float vFreq   = apvts.getRawParameterValue("voiceFreq")->load();
    const float vCarve  = apvts.getRawParameterValue("voiceCarve")->load();
    const float vDuck   = apvts.getRawParameterValue("voiceDuck")->load();
    const float vLift   = apvts.getRawParameterValue("voiceLift")->load();

    const bool chip[3] = {
        apvts.getRawParameterValue("chipLow")->load() > 0.5f,
        apvts.getRawParameterValue("chipMid")->load() > 0.5f,
        apvts.getRawParameterValue("chipAir")->load() > 0.5f };

    float* L = buf.getWritePointer (0);
    float* R = buf.getWritePointer (buf.getNumChannels() > 1 ? 1 : 0);

    // ── Retune eventuel des bandes CLEAN (frequence deplacee dans l'UI) ──
    const char* czId[4] = { "cz0", "cz1", "cz2", "cz3" };
    for (int b = 0; b < kNumCleanBands; ++b)
    {
        const float f = apvts.getRawParameterValue (czId[b])->load();
        bands[b].retuneIfNeeded (sr, f);
        cleanHz[b].store (f);
    }

    // ── 1. Analyse CLEAN (feed-forward, mono) + scope pre-traitement ─────
    for (int s = 0; s < n; ++s)
    {
        const float mono = 0.5f * (L[s] + R[s]);
        pushToScope (mono);
        for (int b = 0; b < kNumCleanBands; ++b)
        {
            const float d = bands[b].det.process (mono);
            bands[b].fast.process (d);
            bands[b].slow.process (d);
        }
        airGuardEnv.process (airGuardDet.process (mono));
    }

    const float depthMax = 3.f + 9.f * clean;   // fond -3 dB, potard jusqu'a -12 dB
    for (int b = 0; b < kNumCleanBands; ++b)
    {
        auto& bd = bands[b];
        const float excessDb = 20.f * std::log10 (juce::jmax (1.0e-9f, bd.fast.v)
                                                / juce::jmax (1.0e-9f, bd.slow.v));
        const float target = excessDb > 4.f
            ? juce::jmin (depthMax, (excessDb - 4.f) * 0.9f) : 0.f;
        bd.cutDb += 0.25f * (target - bd.cutDb);
        cleanDb[b].store (bd.cutDb);
        if (bd.cutDb > 0.05f)
            for (int c = 0; c < 2; ++c) bd.cut[c].setPeaking (sr, bd.freq, bd.q, -bd.cutDb);
        else
            for (int c = 0; c < 2; ++c) bd.cut[c].setIdentity();
    }

    // ── DELTA : capturer le signal AVANT clean pour le mode d'ecoute ─────
    if (delta)
    {
        lowBuf .makeCopyOf (buf, true);   // reutilise lowBuf comme buffer "dry" temporaire
    }

    // ── 2. Couleur (preset) ─────────────────────────────────────────────
    if (preset != lastPreset)
    {
        lastPreset = preset;
        for (int c = 0; c < 2; ++c)
        {
            switch (preset)
            {
                case 1: colA[c].setLowShelf  (sr, 120.f,   2.0f);
                        colB[c].setHighShelf (sr, 8000.f, -1.0f); break;
                case 2: colB[c].setIdentity(); break;   // AIR gere dynamiquement
                default: colA[c].setIdentity(); colB[c].setIdentity(); break;
            }
        }
        lastAirDb = -99.f;
    }
    if (preset == 2)
    {
        const float guard = juce::jlimit (0.f, 1.f, airGuardEnv.v * 10.f);
        const float airDb = 2.5f * (1.f - guard);
        if (std::fabs (airDb - lastAirDb) > 0.1f)
        {
            lastAirDb = airDb;
            for (int c = 0; c < 2; ++c) colA[c].setHighShelf (sr, 10000.f, airDb);
        }
    }

    // ── 3. BOOST : 5 bandes. Chips facade = raccourcis LOW/MID/AIR ───────
    //     gain effectif d'une bande = eq detaille + (chip ? boost*4 : 0)
    float boostSumDb = 0.f;
    for (int i = 0; i < kNumEqBands; ++i)
    {
        const char* eqId[5] = { "eqLow", "eqLowMid", "eqMid", "eqHighMid", "eqAir" };
        float g = apvts.getRawParameterValue (eqId[i])->load();
        if (i == 0 && chip[0]) g += boost * 4.f;   // LOW
        if (i == 2 && chip[1]) g += boost * 4.f;   // MID
        if (i == 4 && chip[2]) g += boost * 4.f;   // AIR
        g = juce::jlimit (-8.f, 8.f, g);
        boostSumDb += juce::jmax (0.f, g);

        if (std::fabs (g - lastEqDb[i]) > 0.05f)
        {
            lastEqDb[i] = g;
            for (int c = 0; c < 2; ++c)
            {
                if (std::fabs (g) < 0.05f)               eq[i][c].setIdentity();
                else if (kEqType[i] == LowShelf)         eq[i][c].setLowShelf  (sr, kEqFreq[i], g);
                else if (kEqType[i] == HighShelf)        eq[i][c].setHighShelf (sr, kEqFreq[i], g);
                else                                     eq[i][c].setPeaking   (sr, kEqFreq[i], 0.9f, g);
            }
        }
    }
    const float boostComp = -boostSumDb * 0.25f;

    // ── 4. Application CLEAN + couleur + boost 5 bandes ──────────────────
    for (int c = 0; c < 2; ++c)
    {
        float* ch = (c == 0 ? L : R);
        for (int s = 0; s < n; ++s)
        {
            float v = ch[s];
            for (int b = 0; b < kNumCleanBands; ++b) v = bands[b].cut[c].process (v);
            v = colA[c].process (v);
            v = colB[c].process (v);
            for (int i = 0; i < kNumEqBands; ++i)    v = eq[i][c].process (v);
            ch[s] = v;
        }
    }

    // ── DELTA : sortir uniquement (dry - wet_clean) et stopper la chaine ─
    if (delta)
    {
        for (int c = 0; c < 2; ++c)
        {
            const float* dry = lowBuf.getReadPointer (c);
            float* ch = (c == 0 ? L : R);
            for (int s = 0; s < n; ++s)
                ch[s] = (dry[s] - ch[s]) * 4.f;   // ce que le CLEAN retire, amplifie
        }
        return;
    }

    // ── 3b. VOICE : Mid/Side adaptatif (voix centre, beat cotes) ─────────
    //     Detecte la presence vocale dans le Mid, creuse la meme bande dans
    //     le Side (fait de la place au beat), leve legerement la voix.
    if (voice > 0.001f)
    {
        if (std::fabs (vFreq - voiceFreqSet) > 1.f)
        {
            voiceFreqSet = vFreq;
            voiceDetBP.setBandpass (sr, vFreq, 1.3f);
            sideBP.setBandpass    (sr, vFreq, 1.3f);
        }
        const float carveMax = juce::Decibels::decibelsToGain (-vCarve) ; // gain cible mini
        const float carveDepth = 1.f - carveMax;      // 0..~0.55 : fraction soustraite
        const float liftMax  = juce::Decibels::decibelsToGain (vLift) - 1.f; // fraction ajoutee
        const float duckMax  = 1.f - juce::Decibels::decibelsToGain (-vDuck);

        for (int s = 0; s < n; ++s)
        {
            float M = 0.5f * (L[s] + R[s]);
            float S = 0.5f * (L[s] - R[s]);

            const float midBand = voiceDetBP.process (M);   // presence du Mid (detect + lift)
            const float vf = voiceFast.process (midBand);
            voiceSlow.process (midBand);
            const float ratio = vf / (voiceSlow.v + 1.0e-6f);
            float pres = juce::jlimit (0.f, 1.f, (ratio - 1.f) * 0.8f);
            presenceSm += 0.02f * (pres - presenceSm);

            const float amt = voice * presenceSm;
            carveSm += 0.01f * (carveDepth * amt - carveSm);
            liftSm  += 0.01f * (liftMax    * amt - liftSm);
            duckSm  += 0.01f * (duckMax    * amt - duckSm);

            const float sideBand = sideBP.process (S);
            S = (S - carveSm * sideBand) * (1.f - duckSm);   // creuse + duck le beat
            M = M + liftSm * midBand;                        // leve la voix

            L[s] = M + S;
            R[s] = M - S;
        }
        voicePresence.store (presenceSm);
    }
    else
    {
        presenceSm += 0.05f * (0.f - presenceSm);
        voicePresence.store (presenceSm);
    }

    // ── 5. GLUE anti-pompage ─────────────────────────────────────────────
    const float ratio    = (preset == 3 ? 2.2f : 1.8f);
    const float ratioExp = 1.f - 1.f / ratio;
    const float thrMul   = (preset == 3 ? 1.26f : 1.41f);
    const int   holdLen  = (int) (0.05 * sr);
    float grDbMeter = 0.f;

    for (int s = 0; s < n; ++s)
    {
        const float mono = 0.5f * (L[s] + R[s]);
        const float det  = glueHP.process (mono);
        const float e    = glueFast.process (det);
        glueSlow.process (det);
        const float rms  = glueRms.process (det);
        const float thr  = juce::jmax (1.0e-6f, rms * thrMul);
        const float ce   = juce::jmax (e, glueSlow.v);

        float g = 1.f;
        if (ce > thr)
        {
            g = std::pow (thr / ce, ratioExp);
            g = juce::jmax (g, 0.708f);   // plafond -3 dB
        }
        if (g < glueGain) { glueGain += 0.12f * (g - glueGain); glueHold = holdLen; }
        else if (glueHold > 0) --glueHold;
        else glueGain += 0.032f * (g - glueGain);
        glueGain = juce::jlimit (0.708f, 1.f, glueGain);

        L[s] *= glueGain; R[s] *= glueGain;
        grDbMeter = juce::jmin (grDbMeter, 20.f * std::log10 (glueGain));
    }
    glueGrDb.store (-grDbMeter);

    // ── 6. Split 120 Hz : sub protege / haut clippe oversample x4 ────────
    for (int c = 0; c < 2; ++c)
    {
        lowBuf .copyFrom (c, 0, buf, c, 0, n);
        highBuf.copyFrom (c, 0, buf, c, 0, n);
    }
    {
        juce::dsp::AudioBlock<float> lb (lowBuf);
        juce::dsp::ProcessContextReplacing<float> lc (lb);
        lrLow.process (lc);
        juce::dsp::AudioBlock<float> hb (highBuf);
        juce::dsp::ProcessContextReplacing<float> hc (hb);
        lrHigh.process (hc);
    }

    for (int s = 0; s < n; ++s)
    {
        const float m = 0.5f * (highBuf.getSample (0, s) + highBuf.getSample (1, s));
        crestPeak.process (m);
        crestRms .process (m);
    }
    const float crestDb = 20.f * std::log10 (juce::jmax (1.0e-9f, crestPeak.v)
                                           / juce::jmax (1.0e-9f, crestRms.v));
    const float maxDriveDb = juce::jmap (juce::jlimit (6.f, 18.f, crestDb), 6.f, 18.f, 5.f, 14.f);
    float effDriveDb = drive * maxDriveDb;
    if (preset == 3) effDriveDb *= 1.15f;

    // Clip du haut, oversample x4. Caractere = forme de la courbe.
    {
        const float pre  = juce::Decibels::decibelsToGain (effDriveDb);
        const float post = juce::Decibels::decibelsToGain (-effDriveDb * 0.82f);
        juce::dsp::AudioBlock<float> hb (highBuf);
        auto ov = os->processSamplesUp (hb);
        for (size_t c = 0; c < ov.getNumChannels(); ++c)
        {
            float* d = ov.getChannelPointer (c);
            for (size_t s = 0; s < ov.getNumSamples(); ++s)
            {
                float x = juce::jlimit (-1.f, 1.f, d[s] * pre);
                float y;
                if (clipCh == 0)       y = 1.5f*x - 0.5f*x*x*x;                 // SOFT (cubique)
                else if (clipCh == 1)  y = std::tanh (1.3f * x) * 1.104f;       // PUNCH
                else                   y = juce::jlimit (-0.98f, 0.98f, x*1.4f); // HARD
                d[s] = y * post;
            }
        }
        os->processSamplesDown (hb);
    }

    // Sub : retard d'alignement + tanh tres doux
    {
        const float subDriveDb = effDriveDb * 0.35f;
        const float pre  = juce::Decibels::decibelsToGain (subDriveDb);
        const float post = juce::Decibels::decibelsToGain (-subDriveDb * 0.9f);
        for (int c = 0; c < 2; ++c)
        {
            float* d = lowBuf.getWritePointer (c);
            for (int s = 0; s < n; ++s)
            {
                subDelay.pushSample (c, d[s]);
                const float v = subDelay.popSample (c);
                d[s] = std::tanh (v * pre) * post;
            }
        }
    }

    for (int c = 0; c < 2; ++c)
    {
        float* ch = (c == 0 ? L : R);
        for (int s = 0; s < n; ++s)
            ch[s] = lowBuf.getSample (c, s) + highBuf.getSample (c, s);
    }

    // ── 7. Sortie + limiteur true-peak + LUFS + trial ───────────────────
    outSm.setTargetValue (juce::Decibels::decibelsToGain (outDb + boostComp));
    const float tpCeil = 0.89f;

    for (int s = 0; s < n; ++s)
    {
        const float g = outSm.getNextValue();
        for (int c = 0; c < 2; ++c)
        {
            float* ch = (c == 0 ? L : R);
            float v = ch[s] * g;

            const float a = std::fabs (v);
            if (a > tpCeil) { const float gg = tpCeil / a; if (gg < tpGain) tpGain = gg; }
            tpGain += (1.f - tpGain) * 0.0009f;
            v = juce::jlimit (-tpCeil, tpCeil, v * tpGain);

#if UM_TRIAL
            if (c == 0)
            {
                ++trialCtr;
                const int period = (int) (60.0 * sr);
                const int muteAt = (int) (58.0 * sr);
                if (trialCtr >= period) trialCtr = 0;
                const float tgt = (trialCtr > muteAt) ? 0.f : 1.f;
                trialGain += (tgt - trialGain) * 0.0005f;
            }
            v *= trialGain;
#endif
            ch[s] = v;
        }

        const float km = kShelf.process (kHP.process (0.5f * (L[s] + R[s])));
        lufsMs += lufsCoef * (km * km - lufsMs);
    }
    lufsShort.store (-0.691f + 10.f * std::log10 (juce::jmax (1.0e-10f, lufsMs)));
}

// ─────────────────────────────────────────────────────────────────────────
void UncertainMasterProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void UncertainMasterProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* UncertainMasterProcessor::createEditor()
{
    return new UncertainMasterEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new UncertainMasterProcessor();
}
