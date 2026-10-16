#include "PluginProcessor.h"
#include "PluginEditor.h"
using namespace forge;

juce::AudioProcessorValueTreeState::ParameterLayout ForgeProcessor::createLayout()
{
    using FP = juce::AudioParameterFloat; using NR = juce::NormalisableRange<float>; using ID = juce::ParameterID;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto pct = [] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; };
    auto db  = [] (float v, int) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; };
    l.add (std::make_unique<FP> (ID { ids::drive, 1 },     "Drive",         NR (0, 100, 0.1f), 35.f, juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<FP> (ID { ids::tone, 1 },      "Tone",          NR (-12, 12, 0.1f), 0.f, juce::AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    l.add (std::make_unique<FP> (ID { ids::wow, 1 },       "Wow / Flutter", NR (0, 100, 0.1f), 0.f,  juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<FP> (ID { ids::character, 1 }, "Character",     NR (0, 100, 0.1f), 50.f, juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<FP> (ID { ids::input, 1 },     "Input",         NR (-12, 12, 0.1f), 0.f, juce::AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    l.add (std::make_unique<FP> (ID { ids::output, 1 },    "Output",        NR (-24, 12, 0.1f), 0.f, juce::AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    l.add (std::make_unique<FP> (ID { ids::mix, 1 },       "Mix",           NR (0, 100, 0.1f), 100.f, juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<juce::AudioParameterBool> (ID { ids::autogain, 1 }, "Auto Gain", true));
    l.add (std::make_unique<juce::AudioParameterChoice> (ID { ids::model, 1 }, "Color Model", juce::StringArray { "TUBE", "TRANSISTOR", "OPTO", "DIODE" }, 0));
    return l;
}

ForgeProcessor::ForgeProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PTZRFORGE", createLayout()), presets (apvts)
{
    for (int i = 0; i < ids::numParams; ++i) { params[(size_t) i] = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (ids::all[i])); raw[(size_t) i] = apvts.getRawParameterValue (ids::all[i]); }
    for (auto& m : ccMap) m.store (-1);
    for (int i = 0; i < ids::numParams; ++i) ccMap[(size_t) (14 + i)].store (i);   // default: CC14..22
    apvts.state.setProperty ("presetName", "INIT", nullptr);
}

bool ForgeProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet(), in = l.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
}

void ForgeProcessor::prepareToPlay (double sr, int)
{
    fs = sr;
    const int stages = sr <= 50000.0 ? 2 : (sr <= 100000.0 ? 1 : 0);          // 4x / 2x / off
    os = std::make_unique<juce::dsp::Oversampling<float>> (2, (size_t) stages,
            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    os->initProcessing ((size_t) kSub);
    os->reset();
    osRate = (float) (sr * (double) (1 << stages));
    dyn.prepare (osRate);
    wowCentre = (int) std::lround (0.003 * sr);
    latency = (int) std::lround (os->getLatencyInSamples()) + wowCentre;
    setLatencySamples (latency);
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) kSub, 2 };
    dryDelay.prepare (spec); dryDelay.setDelay ((float) latency); dryDelay.reset();
    for (int c = 0; c < 2; ++c)
    {
        for (auto* b : { &emph[c], &deemph[c], &preLow[c], &preHigh[c], &postLow[c], &postHigh[c], &bump[c], &lpf[c] }) b->reset();
        dcb[c].setup (sr); dcb[c].reset();
        mod[c].prepare (sr); wowDelay[c].prepare (wowCentre, wowCentre + (int) (0.004 * sr));
        for (auto& s : dynState[c]) s = {};
    }
    auto init = [&] (juce::SmoothedValue<float>& s, float v, double t = 0.03) { s.reset (sr, t); s.setCurrentAndTargetValue (v); };
    init (sDrive, raw[0]->load() * 0.01f); init (sTone, raw[1]->load()); init (sWow, raw[2]->load() * 0.01f);
    init (sChar, raw[3]->load() * 0.01f);  init (sIn, raw[4]->load());
    init (sOut, dbToLin (raw[5]->load()));  init (sMix, raw[6]->load() * 0.01f);  init (sAuto, 0.f, 0.06);
    active = fadeFrom = (Model) juce::jlimit (0, 3, (int) (raw[8]->load() + 0.5f));
    fadeLeft = 0; fadeTotal = std::max (1, (int) (0.02 * osRate));
    lastPreGain = dbToLin (driveToDb (sDrive.getTargetValue()) + sIn.getTargetValue());
}

void ForgeProcessor::handleMidi (juce::MidiBuffer& midi)
{
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isController())
        {
            const int cc = m.getControllerNumber();
            if (const int lp = learnParam.load(); lp >= 0) { clearMapping (lp); ccMap[(size_t) cc].store (lp); learnParam.store (-1); }
            const int pi = ccMap[(size_t) cc].load();
            if (pi < 0) continue;
            const float v = (float) m.getControllerValue() / 127.f;
            float norm = v;
            if (pi == 7) norm = v >= 0.5f ? 1.f : 0.f;                               // auto gain: toggle
            else if (pi == 8) norm = (float) std::min (3, (int) (v * 4.f)) / 3.f;    // model: 4 zones
            params[(size_t) pi]->setValueNotifyingHost (norm);
        }
        else if (m.isProgramChange())
        {
            pendingProgram.store (m.getProgramChangeNumber());
            triggerAsyncUpdate();
        }
    }
}

void ForgeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    handleMidi (midi);
    const int numSamples = buffer.getNumSamples(), inCh = std::max (1, getTotalNumInputChannels()), outCh = getTotalNumOutputChannels();
    if (outCh < 1 || os == nullptr) return;

    bool playing = false;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) playing = pos->getIsPlaying();
    hostPlaying.store (playing, std::memory_order_relaxed);

    sDrive.setTargetValue (raw[0]->load() * 0.01f); sTone.setTargetValue (raw[1]->load()); sWow.setTargetValue (raw[2]->load() * 0.01f);
    sChar.setTargetValue (raw[3]->load() * 0.01f);  sIn.setTargetValue (raw[4]->load());
    sOut.setTargetValue (dbToLin (raw[5]->load())); sMix.setTargetValue (raw[6]->load() * 0.01f);

    float inPk = 0.f, outPk = 0.f;
    for (int pos = 0; pos < numSamples; pos += kSub)
        processSub (buffer, pos, std::min (kSub, numSamples - pos), inCh, outCh, inPk, outPk);

    inPeak.store (std::max (inPeak.load (std::memory_order_relaxed), inPk), std::memory_order_relaxed);
    outPeak.store (std::max (outPeak.load (std::memory_order_relaxed), outPk), std::memory_order_relaxed);
}

void ForgeProcessor::processSub (juce::AudioBuffer<float>& buffer, int pos, int n, int inCh, int outCh, float& inPk, float& outPk)
{
    const float drive = sDrive.skip (n), toneDb = sTone.skip (n), wow = sWow.skip (n), chr = sChar.skip (n),
                inDb = sIn.skip (n), mix = sMix.skip (n);
    const float outG0 = sOut.getCurrentValue(), outG1 = sOut.skip (n);
    const Model target = (Model) juce::jlimit (0, 3, (int) (raw[8]->load() + 0.5f));
    if (target != active) { fadeFrom = active; active = target; fadeLeft = fadeTotal; }

    const float preDb = driveToDb (drive) + inDb, preGain = dbToLin (preDb);
    sAuto.setTargetValue (raw[7]->load() > 0.5f ? autoGainDb (active, chr, preGain) : 0.f);
    const float compLin = dbToLin (sAuto.skip (n));

    // --- coefficients (per 32-sample step; all inputs are already smoothed) ---
    {
        const double sr = fs; const float emphDb = 3.0f + 6.0f * drive;
        const double tPre = 0.5 * 0.35 * toneDb, tPost = 0.5 * 0.65 * toneDb;
        for (int c = 0; c < 2; ++c)
        {
            emph[c].setHighShelf (sr, 3500, emphDb, 0.7);   deemph[c].setHighShelf (sr, 3500, -emphDb, 0.7);
            preLow[c].setLowShelf (sr, 400, -tPre);         preHigh[c].setHighShelf (sr, 2500, tPre);
            postLow[c].setLowShelf (sr, 400, -tPost);       postHigh[c].setHighShelf (sr, 2500, tPost);
            bump[c].setPeak (sr, 65, 0.8 + 1.4 * drive, 0.9);
            lpf[c].setLowpass (sr, 19000.0 - 6000.0 * drive);
        }
    }

    // --- 1. input, pre-emphasis, pre-tone (base rate) ---
    for (int c = 0; c < 2; ++c)
    {
        const float* src = buffer.getReadPointer (std::min (c, inCh - 1)) + pos;
        float* w = work.getWritePointer (c); float* d = dryBuf.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            const float x = sanitize (src[i]);
            if (c < std::max (inCh, 1)) inPk = std::max (inPk, std::abs (x));
            dryDelay.pushSample (c, x); d[i] = dryDelay.popSample (c);
            w[i] = preHigh[c].process (preLow[c].process (emph[c].process (x)));
        }
    }

    // --- 2. nonlinear stage at the oversampled rate ---
    juce::dsp::AudioBlock<float> blk (work.getArrayOfWritePointers(), 2, 0, (size_t) n);
    auto up = os->processSamplesUp (blk);
    const int osN = (int) up.getNumSamples();
    NLParams np; np.character = chr; np.drive = drive;
    for (int c = 0; c < 2; ++c)
    {
        float* d = up.getChannelPointer ((size_t) c);
        for (int i = 0; i < osN; ++i)
        {
            const float g = lastPreGain + (preGain - lastPreGain) * ((float) (i + 1) / (float) osN);
            const float x = d[i] * g;
            const int left = fadeLeft - i;
            float y;
            if (left > 0)
            {
                const float t = 1.0f - (float) left / (float) fadeTotal;
                y = processNL (fadeFrom, np, dyn, dynState[c][(int) fadeFrom], x) * (1.0f - t)
                  + processNL (active,   np, dyn, dynState[c][(int) active],   x) * t;
            }
            else y = processNL (active, np, dyn, dynState[c][(int) active], x);
            d[i] = sanitize (y);
        }
    }
    lastPreGain = preGain; fadeLeft = std::max (0, fadeLeft - osN);
    os->processSamplesDown (blk);

    // --- 3. de-emphasis, post-tone, head bump, HF roll-off, DC, tape speed modulation, gain, mix ---
    const float msToSamp = (float) (fs * 0.001);
    for (int c = 0; c < 2; ++c)
    {
        float* w = work.getWritePointer (c); const float* d = dryBuf.getReadPointer (c);
        float* dst = c < outCh ? buffer.getWritePointer (c) + pos : nullptr;
        for (int i = 0; i < n; ++i)
        {
            float y = deemph[c].process (w[i]);
            y = postHigh[c].process (postLow[c].process (y));
            y = lpf[c].process (bump[c].process (y));
            y = dcb[c].process (y);
            y = wowDelay[c].process (y, mod[c].next (wow, c * 0.37f) * msToSamp);
            y *= compLin;
            const float gOut = outG0 + (outG1 - outG0) * ((float) (i + 1) / (float) n);
            const float o = sanitize ((d[i] * (1.0f - mix) + y * mix) * gOut);
            outPk = std::max (outPk, std::abs (o));
            if (dst) dst[i] = o;
        }
    }
}

juce::AudioProcessorEditor* ForgeProcessor::createEditor() { return new ForgeEditor (*this); }

void ForgeProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::String map;
    for (int c = 0; c < 128; ++c) if (ccMap[(size_t) c].load() >= 0) map << c << "=" << ccMap[(size_t) c].load() << ",";
    apvts.state.setProperty ("midiMap", map, nullptr);
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void ForgeProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            const auto map = apvts.state.getProperty ("midiMap", "").toString();
            if (map.isNotEmpty())
            {
                for (auto& m : ccMap) m.store (-1);
                for (auto& tok : juce::StringArray::fromTokens (map, ",", ""))
                    if (tok.contains ("=")) { const int cc = tok.upToFirstOccurrenceOf ("=", false, false).getIntValue(), pi = tok.fromFirstOccurrenceOf ("=", false, false).getIntValue();
                                              if (cc >= 0 && cc < 128 && pi >= 0 && pi < ids::numParams) ccMap[(size_t) cc].store (pi); }
            }
            presets.sendChangeMessage();
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ForgeProcessor(); }
