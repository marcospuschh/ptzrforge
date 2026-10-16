#include "PluginEditor.h"
#include "BinaryData.h"
using namespace juce;

namespace
{
    constexpr float W = 1536.f, H = 1024.f;
    const Rectangle<float> tapeRect { 150, 100, 1235, 262 };
    const float knobX[4] = { 301, 608, 905, 1197 };
    const Rectangle<float> cardRect[4] = { { 243, 680, 282, 197 }, { 547, 680, 281, 197 }, { 850, 680, 298, 197 }, { 1171, 680, 299, 197 } };
    const float dotX[4] = { 381, 686, 998, 1321 };
    const float inKnobX = 112, outKnobX = 190, smallY = 790;
    const char* tooltipText[] = {
        "DRIVE - how hard the signal hits the saturation stage (soft compression and density at higher settings)",
        "TONE - dark/warm to bright/open. Part of the tilt acts before the saturator, so brighter settings also excite more highs",
        "WOW / FLUTTER - tape speed instability: slow wow plus fast flutter. Zero = no modulation",
        "CHARACTER - reshapes the nonlinearity of the selected model (bias / knee / compression behaviour). Not a second drive",
        "INPUT - level into the saturator. More input = more saturation",
        "OUTPUT - final level. Does not change how much saturation is applied",
        "MIX - dry / wet blend (latency-compensated)" };

    Font mkFont (float size, float kern = 0.f, bool bold = false)
    {
        Font f (FontOptions (size, bold ? Font::bold : Font::plain)); f.setExtraKerningFactor (kern); return f;
    }
    float yToDb (float y) { return y <= 308.f ? -(y - 203.f) / 4.375f : -24.f - (y - 308.f) * (24.f / 26.f); }
}

ForgeEditor::ForgeEditor (ForgeProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    bg = ImageCache::getFromMemory (BinaryData::bg_png, BinaryData::bg_pngSize);
    const void* td[4] = { BinaryData::tape_tube_png, BinaryData::tape_transistor_png, BinaryData::tape_opto_png, BinaryData::tape_diode_png };
    const int ts[4] = { BinaryData::tape_tube_pngSize, BinaryData::tape_transistor_pngSize, BinaryData::tape_opto_pngSize, BinaryData::tape_diode_pngSize };
    for (int i = 0; i < 4; ++i) tapeSrc[i] = ImageCache::getFromMemory (td[i], ts[i]);

    for (int i = 0; i < 7; ++i)
    {
        const bool small = i >= 4;
        knobs[i].reset (new ForgeKnob (proc, i, small));
        auto* prm = proc.apvts.getParameter (ids::all[i]);
        knobAtt[i].reset (new SliderParameterAttachment (*prm, *knobs[i], nullptr));
        knobs[i]->setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
        knobs[i]->setTooltip (tooltipText[i]);
        addAndMakeVisible (*knobs[i]);
    }
    autoBtn.setClickingTogglesState (true);
    autoAtt.reset (new ButtonParameterAttachment (*proc.apvts.getParameter (ids::autogain), autoBtn, nullptr));
    autoBtn.setTooltip ("AUTO GAIN - keeps the loudness of the wet signal roughly constant while you change DRIVE, INPUT, CHARACTER or model");
    autoBtn.painter = [this] (Graphics& g, Rectangle<float> b, bool over, bool)
    {
        b = b.reduced (1.f);
        g.setGradientFill (ColourGradient (Colour (0xff1a1918), 0, b.getY(), Colour (0xff090908), 0, b.getBottom(), false));
        g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
        g.setColour (Colour (over ? 0xff4a4440 : 0xff2e2c2a)); g.drawRoundedRectangle (b, b.getHeight() * 0.5f, 1.2f);
        const auto c = b.getCentre(); const bool on = autoBtn.getToggleState();
        if (on) for (int i = 5; i >= 1; --i) { g.setColour (accent.withAlpha (0.06f * (6 - i))); g.fillEllipse (Rectangle<float> (b.getHeight() * 0.9f + i * 3.f, b.getHeight() * 0.9f + i * 3.f).withCentre (c)); }
        g.setGradientFill (ColourGradient (on ? accent.brighter (0.5f) : Colour (0xff3a2d22), c.x, c.y, on ? accent.darker (0.2f) : Colour (0xff15110d), c.x + b.getHeight() * 0.4f, c.y, true));
        g.fillEllipse (Rectangle<float> (b.getHeight() * 0.62f, b.getHeight() * 0.62f).withCentre (c));
    };
    addAndMakeVisible (autoBtn);

    for (int i = 0; i < 4; ++i)
    {
        cardBtn[i].setTooltip (String ("Select the ") + StringArray { "TUBE", "TRANSISTOR", "OPTO", "DIODE" }[i] + " colour model");
        cardBtn[i].painter = [] (Graphics&, Rectangle<float>, bool, bool) {};   // faces are drawn by the editor (needs the whole scene)
        cardBtn[i].onClick = [this, i] { if (modelAtt) modelAtt->setValueAsCompleteGesture ((float) i); };
        addAndMakeVisible (cardBtn[i]);
    }
    modelAtt.reset (new ParameterAttachment (*proc.apvts.getParameter (ids::model), [this] (float v)
    {
        const int m = jlimit (0, 3, (int) std::lround (v));
        if (m != modelIdx) { tapeFrom = tapeTo; tapeTo = m; blend = 0.f; }
        modelIdx = m; repaint();
    }, nullptr));
    modelAtt->sendInitialUpdate();
    tapeFrom = tapeTo = modelIdx; blend = 1.f; accent = accentFor (modelIdx); lnf.accent = accent;

    // preset bar
    auto arrow = [this] (bool left) { return [this, left] (Graphics& g, Rectangle<float> b, bool over, bool down)
    {
        g.setColour (Colour (over ? 0xff201e1c : 0xff121110)); g.fillRoundedRectangle (b.reduced (1), 5.f);
        g.setColour (Colour (0xff2c2a27)); g.drawRoundedRectangle (b.reduced (1), 5.f, 1.f);
        Path t; const auto c = b.getCentre(); const float s = b.getHeight() * 0.17f * (down ? 0.9f : 1.f), d = left ? -1.f : 1.f;
        t.addTriangle (c.x + d * s, c.y, c.x - d * s, c.y - s * 1.3f, c.x - d * s, c.y + s * 1.3f);
        g.setColour (over ? accent : Colour (0xff8a857d)); g.fillPath (t);
    }; };
    prevBtn.painter = arrow (true); nextBtn.painter = arrow (false);
    prevBtn.setTooltip ("Previous preset"); nextBtn.setTooltip ("Next preset");
    prevBtn.onClick = [this] { proc.presets.previous(); };
    nextBtn.onClick = [this] { proc.presets.next(); };
    nameBtn.setTooltip ("Click to choose a preset");
    nameBtn.painter = [this] (Graphics& g, Rectangle<float> b, bool over, bool)
    {
        g.setColour (Colour (0xff090908)); g.fillRoundedRectangle (b, 6.f);
        g.setColour (Colour (over ? 0xff3a352f : 0xff262422)); g.drawRoundedRectangle (b.reduced (0.5f), 6.f, 1.f);
        g.setColour (Colour (0xffd9d4cb)); g.setFont (mkFont (b.getHeight() * 0.36f, 0.14f));
        g.drawText (proc.presets.getCurrentName().toUpperCase(), b.reduced (14, 0), Justification::centred, true);
    };
    nameBtn.onClick = [this] { showPresetMenu(); };
    saveBtn.setTooltip ("Save the current settings as a user preset");
    saveBtn.painter = [this] (Graphics& g, Rectangle<float> b, bool over, bool)
    {
        g.setColour (Colour (over ? 0xff201e1c : 0xff121110)); g.fillRoundedRectangle (b.reduced (1), 5.f);
        g.setColour (Colour (over ? accent.withAlpha (0.7f) : Colour (0xff2c2a27))); g.drawRoundedRectangle (b.reduced (1), 5.f, 1.f);
        g.setColour (over ? accent : Colour (0xff9a958c)); g.setFont (mkFont (b.getHeight() * 0.32f, 0.22f)); g.drawText ("SAVE", b, Justification::centred);
    };
    saveBtn.onClick = [this] { savePresetDialog(); };
    for (auto* b : { &prevBtn, &nextBtn, &nameBtn, &saveBtn }) addAndMakeVisible (*b);
    proc.presets.addChangeListener (this);

    constrainer.setFixedAspectRatio ((double) W / H);
    constrainer.setSizeLimits (768, 512, 1536, 1024);
    setConstrainer (&constrainer);
    setResizable (true, true);
    setSize (1152, 768);
    startTimerHz (30);
}

ForgeEditor::~ForgeEditor()
{
    proc.presets.removeChangeListener (this);
    stopTimer(); setLookAndFeel (nullptr);
}

Rectangle<int> ForgeEditor::S (float x, float y, float w, float h) const
{
    return { roundToInt (x * sc), roundToInt (y * sc), roundToInt (w * sc), roundToInt (h * sc) };
}

Colour ForgeEditor::accentFor (int m) const
{
    static const uint32 c[] = { 0xffff7a1c, 0xffff8a2b, 0xffffa64d, 0xffff3a1f };   // tube, transistor (hot), opto (soft), diode (red)
    return Colour (c[jlimit (0, 3, m)]);
}

String ForgeEditor::format (int idx, double v) const
{
    switch (idx)
    {
        case 1:  return (v > 0.05 ? "+" : "") + String (v, 1);
        case 4: case 5: return (v > 0.05 ? "+" : "") + String (v, 1) + " dB";
        default: return String (roundToInt (v)) + "%";
    }
}

void ForgeEditor::resized()
{
    sc = (float) getWidth() / W;
    for (int i = 0; i < 4; ++i) knobs[i]->setBounds (S (knobX[i] - 100, 385, 200, 200));
    knobs[4]->setBounds (S (inKnobX - 44, smallY - 44, 88, 88));
    knobs[5]->setBounds (S (outKnobX - 44, smallY - 44, 88, 88));
    knobs[6]->setBounds (S (1419 - 60, 553 - 60, 120, 120));
    autoBtn.setBounds (S (1385, 440, 72, 40));
    for (int i = 0; i < 4; ++i) cardBtn[i].setBounds (S (cardRect[i].getX(), cardRect[i].getY(), cardRect[i].getWidth(), cardRect[i].getHeight()));
    prevBtn.setBounds (S (476, 32, 40, 44)); nameBtn.setBounds (S (522, 32, 420, 44)); nextBtn.setBounds (S (948, 32, 40, 44)); saveBtn.setBounds (S (998, 32, 112, 44));
}

void ForgeEditor::ensureCaches()
{
    float ds = 1.f;
    if (auto* d = Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds())) ds = (float) d->scale;
    if (cacheW == getWidth() && cacheH == getHeight() && cacheScale == ds && bgCache.isValid()) return;
    cacheW = getWidth(); cacheH = getHeight(); cacheScale = ds;
    const float k = sc * ds;
    bgCache = bg.rescaled (roundToInt (W * k), roundToInt (H * k), Graphics::highResamplingQuality);
    for (int i = 0; i < 4; ++i) tapeCache[i] = tapeSrc[i].rescaled (roundToInt (tapeRect.getWidth() * k), roundToInt (tapeRect.getHeight() * k), Graphics::highResamplingQuality);
}

void ForgeEditor::drawMeter (Graphics& g, float x, float db)
{
    for (int i = 0; i < 23; ++i)
    {
        const float y = 199.f + (float) i * 6.1f, segDb = yToDb (y + 1.7f);
        const bool lit = db >= segDb;
        const Colour c = i < 4 ? Colour (0xffff6a22) : (i < 14 ? Colour (0xffffd75a) : Colour (0xffe8741c));
        if (lit) { g.setColour (c.withAlpha (0.18f)); g.fillRoundedRectangle (x - 1.5f, y - 1.f, 15.f, 5.8f, 1.5f); }
        g.setColour (lit ? c : c.withAlpha (0.10f)); g.fillRect (x, y, 12.f, 3.4f);
    }
}

void ForgeEditor::paint (Graphics& g)
{
    ensureCaches();
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    g.drawImage (bgCache, getLocalBounds().toFloat());
    const auto tr = S (tapeRect.getX(), tapeRect.getY(), tapeRect.getWidth(), tapeRect.getHeight()).toFloat();
    g.setOpacity (1.f); g.drawImage (tapeCache[tapeFrom], tr);
    if (blend < 1.f) { g.setOpacity (blend); g.drawImage (tapeCache[tapeTo], tr); }
    g.setOpacity (1.f);
    g.addTransform (AffineTransform::scale (sc));
    paintDynamic (g);
}

void ForgeEditor::paintDynamic (Graphics& g)
{
    const Colour acc = accent;
    // ---- reels: rotating anisotropic sheen clipped to the two reel discs (the disc art itself is part of the photo-real backdrop)
    {
        struct Reel { float cx, cy, rx, ry, ang; } reels[2] = { { 480, 105, 310, 150, angL }, { 1120, 100, 262, 142, angR } };
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (tapeRect.toNearestInt());
        for (auto& r : reels)
        {
            Graphics::ScopedSaveState s2 (g);
            Path disc; disc.addEllipse (r.cx - r.rx, r.cy - r.ry, r.rx * 2, r.ry * 2);
            g.reduceClipRegion (disc);
            g.addTransform (AffineTransform::rotation (r.ang).scaled (1.f, r.ry / r.rx).translated (r.cx, r.cy));
            const float R = r.rx;
            g.setGradientFill (ColourGradient (Colours::transparentWhite, 0, 0, Colours::transparentWhite, R, 0, true));
            ColourGradient cg (acc.interpolatedWith (Colours::white, 0.45f).withAlpha (0.0f), 0, 0, acc.withAlpha (0.0f), R, 0, true);
            cg.addColour (0.30, acc.interpolatedWith (Colours::white, 0.5f).withAlpha (0.05f));
            cg.addColour (0.62, acc.interpolatedWith (Colours::white, 0.5f).withAlpha (0.13f));
            cg.addColour (0.92, acc.interpolatedWith (Colours::white, 0.4f).withAlpha (0.06f));
            g.setGradientFill (cg);
            for (int k = 0; k < 6; ++k)
            {
                const float a0 = (float) k * MathConstants<float>::pi / 3.f, a1 = a0 + 0.20f;
                Path w; w.startNewSubPath (0, 0); w.lineTo (R * std::sin (a0), -R * std::cos (a0));
                w.addCentredArc (0, 0, R, R, 0, a0, a1, false); w.closeSubPath(); g.fillPath (w);
            }
        }
    }
    // ---- head gap glow, driven by the actual output level
    {
        const float lvl = jlimit (0.f, 1.f, (outDb + 40.f) / 40.f), a = 0.10f + 0.30f * lvl + 0.04f * headPulse;
        for (int i = 6; i >= 1; --i) { g.setColour (acc.withAlpha (a * 0.14f * (7 - i) / 6.f)); g.fillRoundedRectangle (Rectangle<float> (697, 281, 154, 19).expanded ((float) i * 2.6f, (float) i * 1.8f), 6.f + (float) i); }
        g.setColour (acc.brighter (0.4f).withAlpha (0.18f + 0.35f * lvl)); g.fillRoundedRectangle (Rectangle<float> (701, 284, 146, 13), 4.f);
    }
    // ---- meters
    drawMeter (g, 78.f, inDb); drawMeter (g, 1415.f, outDb);

    // ---- knob read-outs + small labels
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (acc); g.setFont (mkFont (19.f, 0.05f));
        g.drawText (format (i, knobs[i]->getValue()), Rectangle<float> (knobX[i] - 80, 598, 160, 28), Justification::centred);
    }
    for (int i = 4; i < 6; ++i)
    {
        const float cx = i == 4 ? inKnobX : outKnobX;
        g.setColour (Colour (0xff8d8982)); g.setFont (mkFont (11.f, 0.22f)); g.drawText (i == 4 ? "INPUT" : "OUTPUT", Rectangle<float> (cx - 40, 824, 80, 16), Justification::centred);
        g.setColour (acc); g.setFont (mkFont (12.f, 0.04f)); g.drawText (format (i, knobs[i]->getValue()), Rectangle<float> (cx - 40, 842, 80, 16), Justification::centred);
    }
    if (proc.learnParam.load() >= 0) { g.setColour (acc); g.setFont (mkFont (11.f, 0.2f)); g.drawText ("MIDI LEARN - MOVE A CONTROLLER", Rectangle<float> (476, 80, 634, 16), Justification::centred); }

    // ---- colour model cards
    for (int i = 0; i < 4; ++i)
    {
        const auto r = cardRect[i]; const bool sel = i == modelIdx, over = cardBtn[i].isOver();
        const Colour ac = accentFor (i);
        if (sel)
        {
            for (int k = 7; k >= 1; --k) { g.setColour (ac.withAlpha (0.045f * (8 - k) / 7.f)); g.drawRoundedRectangle (r.expanded ((float) k * 2.4f), 8.f + (float) k * 1.5f, 3.f); }
            g.setColour (ac.withAlpha (0.06f)); g.fillRoundedRectangle (r, 7.f);
            g.setGradientFill (ColourGradient (ac.withAlpha (0.20f), r.getCentreX(), r.getY() + 80, Colours::transparentBlack, r.getCentreX() + 135, r.getY() + 80, true));
            g.fillRect (r.reduced (4, 8).withHeight (110.f));
            g.setColour (ac); g.drawRoundedRectangle (r.reduced (1.f), 7.f, 2.2f);
        }
        else { g.setColour (over ? Colour (0xff3a3835) : Colour (0xff262523)); g.drawRoundedRectangle (r.reduced (0.5f), 7.f, 1.f); if (over) { g.setColour (Colours::black.withAlpha (0.0f)); } }
        const Point<float> d (dotX[i], 862.f);
        if (sel) { for (int k = 4; k >= 1; --k) { g.setColour (ac.withAlpha (0.10f * (5 - k))); g.fillEllipse (Rectangle<float> ((float) k * 4.f + 10.f, (float) k * 4.f + 10.f).withCentre (d)); }
                   g.setColour (ac.brighter (0.5f)); g.fillEllipse (Rectangle<float> (10.f, 10.f).withCentre (d)); }
        else { g.setColour (Colour (0xff383634)); g.fillEllipse (Rectangle<float> (9.f, 9.f).withCentre (d)); g.setColour (Colour (0xff1a1918)); g.drawEllipse (Rectangle<float> (9.f, 9.f).withCentre (d), 1.f); }
    }
}

void ForgeEditor::timerCallback()
{
    constexpr float dt = 1.f / 30.f;
    auto toDb = [] (float v) { return Decibels::gainToDecibels (v, -90.f); };
    const float ip = proc.inPeak.exchange (0.f), op = proc.outPeak.exchange (0.f);
    const float iDb = toDb (ip), oDb = toDb (op);
    inDb  = iDb > inDb ? iDb : std::max (iDb, inDb - 1.1f);      // instant attack, ~33 dB/s release
    outDb = oDb > outDb ? oDb : std::max (oDb, outDb - 1.1f);
    quietFrames = std::max (iDb, oDb) > -70.f ? 0 : quietFrames + 1;
    const bool moving = proc.hostPlaying.load() || quietFrames < 20;      // host transport, or audio is flowing (standalone / live input)
    spin += ((moving ? 1.f : 0.f) - spin) * 0.08f;                          // ease in / out: reels coast to a stop
    angL -= spin * 2.6f * dt; angR -= spin * 2.2f * dt;
    headPulse = std::sin (angL * 3.f);

    bool accentMoved = false;
    if (blend < 1.f) { blend = std::min (1.f, blend + 0.06f); }
    const Colour target = accentFor (modelIdx);
    if (accent != target)
    {
        auto near = [&] (uint8 a, uint8 b) { return std::abs ((int) a - (int) b) < 3; };
        accent = (near (accent.getRed(), target.getRed()) && near (accent.getGreen(), target.getGreen()) && near (accent.getBlue(), target.getBlue()))
                     ? target : accent.interpolatedWith (target, 0.18f);
        lnf.accent = accent; accentMoved = true;
    }
    if (accentMoved) for (auto& k : knobs) k->repaint();
    repaint (S (tapeRect.getX(), tapeRect.getY(), tapeRect.getWidth(), tapeRect.getHeight()));
    repaint (S (60, 190, 90, 160)); repaint (S (1395, 190, 90, 160));
    if (accentMoved) repaint();
}

void ForgeEditor::showPresetMenu()
{
    PopupMenu m; auto& pm = proc.presets; bool userHeader = false;
    for (int i = 0; i < pm.size(); ++i)
    {
        if (! pm.get (i).factory && ! userHeader) { m.addSeparator(); m.addSectionHeader ("USER"); userHeader = true; }
        m.addItem (i + 1, pm.get (i).name, true, i == pm.getCurrent());
    }
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&nameBtn).withMinimumWidth (nameBtn.getWidth()), [this, safe = SafePointer<ForgeEditor> (this)] (int r)
    { if (safe != nullptr && r > 0) proc.presets.apply (r - 1); });
}

void ForgeEditor::savePresetDialog()
{
    auto* w = new AlertWindow ("Save preset", "Preset name:", MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", proc.presets.getCurrentName());
    w->addButton ("Save", 1, KeyPress (KeyPress::returnKey)); w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    w->enterModalState (true, ModalCallbackFunction::create ([this, safe = SafePointer<ForgeEditor> (this), w] (int r)
    { if (safe != nullptr && r == 1) proc.presets.saveUser (w->getTextEditorContents ("name")); }), true);
}
