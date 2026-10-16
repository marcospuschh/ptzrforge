#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ForgeLookAndFeel.h"

// Generic custom-painted button (all faces are drawn by the editor, no stock JUCE widgets).
class PaintButton : public juce::Button
{
public:
    std::function<void (juce::Graphics&, juce::Rectangle<float>, bool over, bool down)> painter;
    PaintButton() : juce::Button ({}) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paintButton (juce::Graphics& g, bool over, bool down) override { if (painter) painter (g, getLocalBounds().toFloat(), over, down); }
};

class ForgeKnob : public juce::Slider
{
public:
    ForgeKnob (ForgeProcessor& p, int idx, bool small) : proc (p), paramIndex (idx)
    {
        setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);   // 7:30 -> 4:30
        setMouseDragSensitivity (260);
        getProperties().set ("small", small);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m; const int cc = proc.ccForParam (paramIndex);
            m.addItem (1, "MIDI Learn  (move a controller)");
            m.addItem (2, cc >= 0 ? "Clear MIDI mapping (CC " + juce::String (cc) + ")" : "No MIDI mapping", cc >= 0);
            m.addItem (3, "Reset to default");
            m.showMenuAsync ({}, [this, safe = juce::Component::SafePointer<ForgeKnob> (this)] (int r)
            {
                if (safe == nullptr) return;
                if (r == 1) proc.learnParam.store (paramIndex);
                else if (r == 2) proc.clearMapping (paramIndex);
                else if (r == 3) setValue (getDoubleClickReturnValue(), juce::sendNotificationSync);
            });
            return;
        }
        juce::Slider::mouseDown (e);
    }
private:
    ForgeProcessor& proc; int paramIndex;
};

class ForgeEditor : public juce::AudioProcessorEditor, private juce::Timer, private juce::ChangeListener
{
public:
    explicit ForgeEditor (ForgeProcessor&);
    ~ForgeEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void ensureCaches();
    void paintDynamic (juce::Graphics&);
    void drawMeter (juce::Graphics&, float x, float db);
    juce::Rectangle<int> S (float x, float y, float w, float h) const;   // design -> pixels
    juce::Colour accentFor (int model) const;
    juce::String format (int idx, double v) const;
    void showPresetMenu();
    void savePresetDialog();

    ForgeProcessor& proc;
    ForgeLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    juce::ComponentBoundsConstrainer constrainer;

    std::unique_ptr<ForgeKnob> knobs[7];            // drive tone wow character input output mix
    std::unique_ptr<juce::SliderParameterAttachment> knobAtt[7];
    PaintButton autoBtn, prevBtn, nextBtn, nameBtn, saveBtn, cardBtn[4];
    std::unique_ptr<juce::ButtonParameterAttachment> autoAtt;
    std::unique_ptr<juce::ParameterAttachment> modelAtt;

    juce::Image bg, tapeSrc[4], bgCache, tapeCache[4];
    int cacheW = 0, cacheH = 0; float cacheScale = 1.f;
    float sc = 1.f;

    int modelIdx = 0, tapeFrom = 0, tapeTo = 0; float blend = 1.f;
    juce::Colour accent { 0xffff7a1c };
    float inDb = -90.f, outDb = -90.f, angL = 0.f, angR = 1.f, spin = 0.f, headPulse = 0.f; int quietFrames = 0;
};
