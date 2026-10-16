#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

// Draws the metal knobs. The static body (bezel, brushed cap, knurling, specular sheen) is rendered once per size
// into a 2x supersampled image; the illuminated arc and indicator are drawn live in the current accent colour.
class ForgeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    juce::Colour accent { 0xffff7a1c };

    ForgeLookAndFeel()
    {
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff141413));
        setColour (juce::PopupMenu::textColourId, juce::Colour (0xffc9c6c0));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff2a1a0e));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colour (0xffff9a4a));
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff1a1917));
        setColour (juce::TooltipWindow::textColourId, juce::Colour (0xffd8d4cc));
        setColour (juce::TooltipWindow::outlineColourId, juce::Colour (0xff3a2a1a));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float startA, float endA, juce::Slider& s) override
    {
        const float R = (float) std::min (w, h) * 0.5f;
        const float cx = (float) x + (float) w * 0.5f, cy = (float) y + (float) h * 0.5f;
        const int key = (int) std::round (R * 2.0f);

        auto& body = bodyFor (key);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (body, juce::Rectangle<float> (cx - R, cy - R, R * 2, R * 2));

        const float ang = startA + pos * (endA - startA);
        const bool small = (bool) s.getProperties().getWithDefault ("small", false);
        const float arcR = R * 0.88f, tw = R * (small ? 0.05f : 0.035f);

        auto arc = [&] (float a0, float a1)
        {
            juce::Path p; p.addCentredArc (cx, cy, arcR, arcR, 0.0f, a0, a1, true); return p;
        };
        // track
        g.setColour (juce::Colours::white.withAlpha (0.055f));
        g.strokePath (arc (startA, endA), juce::PathStrokeType (tw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // lit arc with layered glow
        if (pos > 0.002f)
        {
            const auto p = arc (startA, ang);
            const float gw[] = { 0.20f, 0.13f, 0.08f }, ga[] = { 0.05f, 0.09f, 0.20f };
            for (int i = 0; i < 3; ++i)
            {
                g.setColour (accent.withAlpha (ga[i]));
                g.strokePath (p, juce::PathStrokeType (R * gw[i], juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour (accent.interpolatedWith (juce::Colours::white, 0.12f));
            g.strokePath (p, juce::PathStrokeType (tw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        // indicator
        const float r1 = R * 0.20f, r2 = R * 0.58f, sa = std::sin (ang), ca = -std::cos (ang);
        juce::Line<float> ln (cx + sa * r1, cy + ca * r1, cx + sa * r2, cy + ca * r2);
        g.setColour (accent.withAlpha (0.28f));  g.drawLine (ln, R * 0.10f);
        g.setColour (accent.withAlpha (0.55f));  g.drawLine (ln, R * 0.055f);
        g.setColour (accent.interpolatedWith (juce::Colours::white, 0.55f)); g.drawLine (ln, R * 0.028f);
    }

private:
    std::map<int, juce::Image> bodies;

    juce::Image& bodyFor (int key)
    {
        auto it = bodies.find (key);
        if (it != bodies.end()) return it->second;
        const int S = std::max (8, key * 2);                 // 2x supersampling
        juce::Image img (juce::Image::ARGB, S, S, true);
        juce::Graphics g (img);
        const float c = (float) S * 0.5f, R = c;
        auto circle = [&] (float r) { juce::Path p; p.addEllipse (c - r, c - r, r * 2, r * 2); return p; };

        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.75f), c, c, juce::Colours::transparentBlack, c, c + R, true));
        g.fillEllipse (c - R * 0.98f, c - R * 0.94f, R * 1.96f, R * 1.96f);                               // contact shadow
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2a27), c, c - R * 0.74f, juce::Colour (0xff080807), c, c + R * 0.74f, false));
        g.fillEllipse (c - R * 0.78f, c - R * 0.78f, R * 1.56f, R * 1.56f);                               // bezel
        g.setColour (juce::Colours::white.withAlpha (0.10f)); g.drawEllipse (c - R * 0.78f, c - R * 0.78f, R * 1.56f, R * 1.56f, R * 0.012f);
        g.setColour (juce::Colour (0xff040404)); g.fillEllipse (c - R * 0.71f, c - R * 0.71f, R * 1.42f, R * 1.42f);   // gap
        const float cr = R * 0.67f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3d3b38), c - cr * 0.7f, c - cr * 0.9f, juce::Colour (0xff0b0b0a), c + cr * 0.7f, c + cr * 0.9f, false));
        g.fillPath (circle (cr));                                                                          // cap

        // brushed concentric rings
        juce::Random rnd (1234);
        for (int k = 2; k < 64; ++k)
        {
            const float r = cr * (float) k / 64.0f;
            g.setColour ((rnd.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.015f + 0.04f * rnd.nextFloat()));
            g.drawEllipse (c - r, c - r, r * 2, r * 2, R * 0.006f);
        }
        // conic specular sheen (two opposing lobes), drawn as thin wedges
        for (int a = 0; a < 180; ++a)
        {
            const float deg = (float) a * 2.0f, rad = juce::degreesToRadians (deg);
            auto lobe = [&] (float centre) { float d = std::cos (juce::degreesToRadians (deg - centre)); return d > 0 ? std::pow (d, 7.0f) : 0.0f; };
            const float al = 0.16f * lobe (35.0f) + 0.10f * lobe (215.0f);
            if (al < 0.004f) continue;
            juce::Path w; w.startNewSubPath (c, c);
            w.lineTo (c + cr * std::sin (rad), c - cr * std::cos (rad));
            w.lineTo (c + cr * std::sin (rad + 0.037f), c - cr * std::cos (rad + 0.037f)); w.closeSubPath();
            g.setColour (juce::Colours::white.withAlpha (al)); g.fillPath (w);
        }
        // knurled edge
        for (int k = 0; k < 120; ++k)
        {
            const float rad = juce::MathConstants<float>::twoPi * (float) k / 120.0f;
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.drawLine (c + std::sin (rad) * cr * 0.955f, c - std::cos (rad) * cr * 0.955f, c + std::sin (rad) * cr * 0.995f, c - std::cos (rad) * cr * 0.995f, R * 0.008f);
        }
        g.setColour (juce::Colours::white.withAlpha (0.14f)); g.drawEllipse (c - cr, c - cr, cr * 2, cr * 2, R * 0.010f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff161615), c, c, juce::Colour (0x00161615), c + R * 0.16f, c, true));
        g.fillEllipse (c - R * 0.16f, c - R * 0.16f, R * 0.32f, R * 0.32f);                                // centre dimple
        return bodies.emplace (key, img).first->second;
    }
};
