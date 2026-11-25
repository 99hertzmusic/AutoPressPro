// AutoPress (Pro build) – UI (99Hertz hardware look: brushed panel, knurled dials, lit keys, LED screens)
#include "PluginEditor.h"
#include "BinaryData.h"
#include <cmath>

#if JucePlugin_Build_Standalone
 #include <juce_audio_utils/juce_audio_utils.h>
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

using APP = AutoPressProAudioProcessor;

namespace Pal
{
    static const juce::Colour panelTop  { 0xff202124 };
    static const juce::Colour panelBot  { 0xff141517 };
    static const juce::Colour screen    { 0xff060707 };
    static const juce::Colour silk      { 0xffe4e5e7 };
    static const juce::Colour silkMid   { 0xffb4b7bc };
    static const juce::Colour silkLo    { 0xff7d8086 };
    static const juce::Colour lampOff   { 0xff3a3b3f };
    static const juce::Colour accent    { 0xff00d4ff };   // AutoPress cyan
    static const juce::Colour warn      { 0xffff9a2e };
    static const juce::Colour level     { 0xfff4f1e6 };
}

// ── Fonts (embedded, OFL: Barlow Condensed + Share Tech Mono) ────────────────
namespace Fonts
{
    static juce::Typeface::Ptr load (const char* data, int size) { return juce::Typeface::createSystemTypefaceFor (data, (size_t) size); }
    static juce::Typeface::Ptr bold()  { static auto t = load (BinaryData::BarlowCondensedBold_ttf,     BinaryData::BarlowCondensedBold_ttfSize);     return t; }
    static juce::Typeface::Ptr semi()  { static auto t = load (BinaryData::BarlowCondensedSemiBold_ttf, BinaryData::BarlowCondensedSemiBold_ttfSize); return t; }
    static juce::Typeface::Ptr med()   { static auto t = load (BinaryData::BarlowCondensedMedium_ttf,   BinaryData::BarlowCondensedMedium_ttfSize);   return t; }
    static juce::Typeface::Ptr mono()  { static auto t = load (BinaryData::ShareTechMonoRegular_ttf,    BinaryData::ShareTechMonoRegular_ttfSize);    return t; }
    static juce::Font make (juce::Typeface::Ptr t, float size, float spacing = 0.0f)
    {
        return juce::Font (juce::FontOptions (t).withPointHeight (size).withKerningFactor (spacing));
    }
    static juce::Font silkBold (float s, float sp = 0.2f)  { return make (bold(), s, sp); }
    static juce::Font silkSemi (float s, float sp = 0.16f) { return make (semi(), s, sp); }
    static juce::Font silkMed  (float s, float sp = 0.14f) { return make (med(), s, sp); }
    static juce::Font led      (float s, float sp = 0.06f) { return make (mono(), s, sp); }
}

static float textWidth (const juce::Font& f, const juce::String& t)
{
    juce::GlyphArrangement ga; ga.addLineOfText (f, t, 0, 0);
    return ga.getBoundingBox (0, -1, true).getWidth();
}

static void glowText (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Justification j, juce::Colour c)
{
    g.setColour (c.withAlpha (0.18f));
    for (auto d : { juce::Point<float> (-1.0f, 0.0f), { 1.0f, 0.0f }, { 0.0f, -1.0f }, { 0.0f, 1.0f } })
        g.drawText (t, r.translated (d.x, d.y), j, false);
    g.setColour (c);
    g.drawText (t, r, j, false);
}

static void glowPath (juce::Graphics& g, const juce::Path& p, juce::Colour c, float w)
{
    g.setColour (c.withAlpha (0.12f)); g.strokePath (p, juce::PathStrokeType (w * 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (c.withAlpha (0.25f)); g.strokePath (p, juce::PathStrokeType (w * 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (c);                   g.strokePath (p, juce::PathStrokeType (w,        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

static void drawScreen (juce::Graphics& g, juce::Rectangle<float> r, float rad = 5.0f)
{
    g.setColour (juce::Colours::white.withAlpha (0.09f));
    g.fillRoundedRectangle (r.translated (0, 1.0f), rad);
    g.setColour (Pal::screen);
    g.fillRoundedRectangle (r, rad);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black, 0, r.getY(), juce::Colours::transparentBlack, 0, r.getY() + 10.0f, false));
    g.fillRoundedRectangle (r, rad);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (r, rad, 1.0f);
}

static void drawScrew (juce::Graphics& g, juce::Point<float> c, float angle)
{
    const float R = 7.0f;
    juce::ColourGradient gr (juce::Colour (0xff7a7c81), c.x - 1.5f, c.y - 2.5f, juce::Colour (0xff1c1d1f), c.x + R, c.y + R, true);
    gr.addColour (0.6, juce::Colour (0xff3b3c40));
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.fillEllipse (c.x - R, c.y - R + 1.0f, 2 * R, 2 * R);
    g.setGradientFill (gr);
    g.fillEllipse (c.x - R, c.y - R, 2 * R, 2 * R);
    juce::Path slot;
    slot.addRoundedRectangle (-5.0f, -1.0f, 10.0f, 2.0f, 1.0f);
    g.setColour (juce::Colour (0xff101012));
    g.fillPath (slot, juce::AffineTransform::rotation (angle).translated (c));
}

static void padlock (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    auto body = r.removeFromBottom (r.getHeight() * 0.58f);
    juce::Path shackle;
    shackle.addCentredArc (body.getCentreX(), body.getY(), body.getWidth() * 0.3f, r.getHeight() * 0.85f, 0,
                           -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
    g.setColour (c);
    g.strokePath (shackle, juce::PathStrokeType (1.4f));
    g.fillRoundedRectangle (body, 1.5f);
}

static constexpr float dialStart = juce::MathConstants<float>::pi * 1.25f, dialEnd = juce::MathConstants<float>::pi * 2.75f;

// ── LookAndFeel: knurled dials + hardware keys ───────────────────────────────
class HardwareLookAndFeel : public juce::LookAndFeel_V4
{
public:
    HardwareLookAndFeel() { setColourScheme (juce::LookAndFeel_V4::getDarkColourScheme()); }

    const juce::Image& knobBody (int D)
    {
        auto& img = bodies[D];
        if (img.isValid()) return img;
        const float sc = 2.0f;
        img = juce::Image (juce::Image::ARGB, (int) (D * sc), (int) (D * sc) + (int) (D * 0.2f * sc), true);
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (sc));
        const float R = D * 0.5f;
        const juce::Point<float> c (R, R);
        const float rs = R * 0.705f, rc = R * 0.564f;
        for (int i = 0; i < 6; ++i)
        {
            g.setColour (juce::Colours::black.withAlpha (0.10f));
            const float e = rs + i * 1.2f;
            g.fillEllipse (c.x - e, c.y - e + R * 0.14f, 2 * e, 2 * e);
        }
        juce::Path skirt; skirt.addEllipse (c.x - rs, c.y - rs, 2 * rs, 2 * rs);
        g.setColour (juce::Colour (0xff0d0d0f)); g.fillPath (skirt);
        g.setColour (juce::Colour (0xff303135));
        for (int k = 0; k < 60; ++k)
        {
            juce::Path w;
            const float a0 = k * juce::MathConstants<float>::twoPi / 60.0f, a1 = a0 + juce::MathConstants<float>::twoPi / 120.0f;
            w.addPieSegment (c.x - rs, c.y - rs, 2 * rs, 2 * rs, a0, a1, 0.0f);
            g.fillPath (w);
        }
        g.setColour (juce::Colour (0xff050506)); g.drawEllipse (c.x - rs, c.y - rs, 2 * rs, 2 * rs, 1.0f);
        juce::ColourGradient sh (juce::Colours::white.withAlpha (0.16f), 0, c.y - rs, juce::Colours::black.withAlpha (0.42f), 0, c.y + rs, false);
        sh.addColour (0.38, juce::Colours::transparentWhite);
        g.setGradientFill (sh); g.fillPath (skirt);
        juce::ColourGradient cg (juce::Colour (0xff404145), c.x, c.y - rc * 0.48f, juce::Colour (0xff17181a), c.x, c.y + rc * 1.1f, true);
        cg.addColour (0.52, juce::Colour (0xff26272a));
        g.setGradientFill (cg);
        g.fillEllipse (c.x - rc, c.y - rc, 2 * rc, 2 * rc);
        g.setColour (juce::Colours::white.withAlpha (0.2f));
        g.drawEllipse (c.x - rc + 0.5f, c.y - rc + 0.5f, 2 * rc - 1.0f, 2 * rc - 1.0f, 0.6f);
        g.setColour (juce::Colour (0xff08080a));
        g.drawEllipse (c.x - rc, c.y - rc, 2 * rc, 2 * rc, 1.0f);
        return img;
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        const int D = juce::jmin (w, h);
        const float R = D * 0.5f;
        const juce::Point<float> c ((float) x + w * 0.5f, (float) y + R);
        const auto& props = s.getProperties();
        if (props.contains ("displayPos")) pos = (float) props["displayPos"];
        const int steps = (int) props.getWithDefault ("steps", 0);
        const bool locked = (bool) props.getWithDefault ("locked", false);
        const float angle = startAngle + juce::jlimit (0.0f, 1.0f, pos) * (endAngle - startAngle);
        if (locked) g.beginTransparencyLayer (0.55f);

        const int ticks = steps > 1 ? steps - 1 : 10;
        for (int k = 0; k <= ticks; ++k)
        {
            const float a = startAngle + k * (endAngle - startAngle) / ticks;
            const float sx = std::sin (a), sy = -std::cos (a);
            const bool strong = steps > 1 || k == 0 || k == ticks;
            g.setColour (juce::Colour (0xffd4d6da).withAlpha (strong ? 0.9f : 0.55f));
            g.drawLine (c.x + sx * R * 0.86f, c.y + sy * R * 0.86f, c.x + sx * R * 0.99f, c.y + sy * R * 0.99f, steps > 1 ? 1.6f : 1.3f);
        }
        {
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, R * 0.79f, R * 0.79f, 0, startAngle, angle, true);
            g.setColour (Pal::accent.withAlpha (0.25f));
            g.strokePath (arc, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (Pal::accent);
            g.strokePath (arc, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        const auto& body = knobBody (D);
        g.drawImage (body, juce::Rectangle<float> (c.x - R, c.y - R, (float) D, (float) body.getHeight() / 2.0f));

        const float rc = R * 0.564f;
        const float sx = std::sin (angle), sy = -std::cos (angle);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawLine (c.x + sx * rc * 0.2f, c.y + sy * rc * 0.2f + 1.0f, c.x + sx * rc * 0.88f, c.y + sy * rc * 0.88f + 1.0f, D * 0.045f);
        g.setColour (juce::Colour (0xfff4f4f1));
        g.drawLine (c.x + sx * rc * 0.2f, c.y + sy * rc * 0.2f, c.x + sx * rc * 0.88f, c.y + sy * rc * 0.88f, D * 0.045f);
        if (locked)
        {
            g.endTransparencyLayer();
            // padlock badge on the cap
            auto badge = juce::Rectangle<float> (c.x - 9.0f, c.y - 10.0f, 18.0f, 20.0f);
            g.setColour (juce::Colour (0xff17181a).withAlpha (0.85f));
            g.fillEllipse (c.x - 11.0f, c.y - 11.0f, 22.0f, 22.0f);
            padlock (g, badge.reduced (3.5f, 3.0f), Pal::silk);
        }
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        if (b.getProperties().contains ("pill"))
        {
            auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            const bool warnCol = (bool) b.getProperties()["warn"];
            const auto c = warnCol ? Pal::warn : Pal::accent;
            g.setColour (c.withAlpha (over ? 0.22f : 0.12f));
            g.fillRoundedRectangle (r, r.getHeight() / 2);
            g.setColour (c.withAlpha (0.7f));
            g.drawRoundedRectangle (r, r.getHeight() / 2, 1.0f);
            return;
        }
        auto r = b.getLocalBounds().toFloat().reduced (1.0f).withTrimmedBottom (3.0f);
        const bool on = b.getToggleState() || down;
        if (on) r.translate (0.0f, 2.0f);
        const float rad = 6.0f;
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (r.translated (0.0f, on ? 1.0f : 3.0f), rad);
        g.setGradientFill (juce::ColourGradient (juce::Colour (over ? 0xff34353a : 0xff2e2f33), 0, r.getY(),
                                                 juce::Colour (0xff1c1d20), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, rad);
        g.setColour (juce::Colours::white.withAlpha (0.13f));
        g.drawLine (r.getX() + rad, r.getY() + 1.0f, r.getRight() - rad, r.getY() + 1.0f, 1.0f);
        g.setColour (juce::Colour (0xff050506));
        g.drawRoundedRectangle (r, rad, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down) override
    {
        if (b.getProperties().contains ("pill"))
        {
            const bool warnCol = (bool) b.getProperties()["warn"];
            g.setFont (Fonts::led (11.0f, 0.06f));
            g.setColour (warnCol ? Pal::warn : Pal::accent);
            g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
            return;
        }
        const bool locked = (bool) b.getProperties().getWithDefault ("locked", false);
        const bool on  = b.getToggleState() && ! locked;
        const bool savedLocked = locked && (bool) b.getProperties().getWithDefault ("saved", false);   // upgrade setting saved in the project
        const bool lit = on || ((bool) b.getProperties().getWithDefault ("lit", false) && ! locked);
        auto r = b.getLocalBounds().toFloat().reduced (1.0f).withTrimmedBottom (3.0f);
        if (on || down) r.translate (0.0f, 2.0f);
        const bool small = r.getHeight() < 34.0f;

        const float lw = small ? 20.0f : 30.0f, lh = small ? 3.0f : 4.0f;
        auto lamp = juce::Rectangle<float> (r.getCentreX() - lw / 2, r.getY() + (small ? 6.0f : 9.0f), lw, lh);
        if (lit)
        {
            g.setColour (Pal::accent.withAlpha (0.25f));
            g.fillRoundedRectangle (lamp.expanded (3.0f, 2.5f), 3.0f);
            g.setColour (Pal::accent);
        }
        else g.setColour (savedLocked ? Pal::silkLo : Pal::lampOff);
        g.fillRoundedRectangle (lamp, lh / 2);

        if (locked)
            padlock (g, { r.getRight() - 17.0f, r.getY() + 6.0f, 9.0f, 11.0f }, Pal::silkLo);
        g.setFont (Fonts::silkSemi (small ? 12.0f : 14.5f, 0.14f));
        g.setColour (on ? juce::Colour (0xfff2f2ef) : savedLocked ? Pal::silk.withAlpha (0.55f) : Pal::silkMid.withAlpha (b.isEnabled() ? 1.0f : 0.5f));
        g.drawText (b.getButtonText(), r.withTrimmedTop (lamp.getBottom() - r.getY()), juce::Justification::centred);
    }

private:
    std::map<int, juce::Image> bodies;
};

// ── SegmentControl ────────────────────────────────────────────────────────────
SegmentControl::SegmentControl (juce::RangedAudioParameter& param, juce::StringArray labels)
    : attachment (param, [this] (float v) { showValue ((int) std::round (v)); })
{
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* b = buttons.add (new juce::TextButton (labels[i]));
        b->onClick = [this, i]
        {
            if (allowValue && ! allowValue (i)) { showValue (shown); return; }
            attachment.setValueAsCompleteGesture ((float) i);
        };
        addAndMakeVisible (b);
    }
    attachment.sendInitialUpdate();
}

void SegmentControl::showValue (int v)
{
    shown = v;
    for (int i = 0; i < buttons.size(); ++i)
        if (buttons[i]->getToggleState() != (i == v)) buttons[i]->setToggleState (i == v, juce::dontSendNotification);
}

void SegmentControl::markSaved (int value)
{
    for (int i = 0; i < buttons.size(); ++i)
        if ((bool) buttons[i]->getProperties().getWithDefault ("saved", false) != (i == value))
        {
            buttons[i]->getProperties().set ("saved", i == value);
            buttons[i]->repaint();
        }
}

void SegmentControl::setLocked (int value, bool locked)
{
    if (auto* b = buttons[value])
        if ((bool) b->getProperties().getWithDefault ("locked", false) != locked)
        {
            b->getProperties().set ("locked", locked);
            b->repaint();
        }
}

void SegmentControl::resized()
{
    auto r = getLocalBounds();
    const int cols = buttons.size();
    const int w = (r.getWidth() - gap * (cols - 1)) / juce::jmax (1, cols);
    for (int i = 0; i < cols; ++i)
        buttons[i]->setBounds (r.getX() + i * (w + gap), r.getY(), w, r.getHeight());
}

// ── GR scope ──────────────────────────────────────────────────────────────────
static constexpr float scopeMaxGr = 18.0f;

void GrScope::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    drawScreen (g, area);
    g.setFont (Fonts::led (12.0f, 0.14f));
    glowText (g, "GAIN REDUCTION", area.reduced (16.0f, 9.0f).withHeight (14.0f), juce::Justification::centredLeft, Pal::accent.withAlpha (0.55f));
    g.setFont (Fonts::led (11.0f, 0.08f));
    auto legend = area.reduced (16.0f, 9.0f).withHeight (14.0f);
    g.setColour (Pal::accent.withAlpha (0.8f)); g.drawText ("GR", legend, juce::Justification::centredRight);
    g.setColour (Pal::level.withAlpha (0.4f));  g.drawText ("IN", legend.withTrimmedRight (34.0f), juce::Justification::centredRight);

    const auto r = area.withTrimmedTop (30.0f).reduced (14.0f, 0.0f).withTrimmedBottom (12.0f).withTrimmedRight (22.0f);
    // dB grid
    g.setFont (Fonts::led (10.0f, 0.04f));
    for (float db : { 3.0f, 6.0f, 12.0f })
    {
        const float y = r.getY() + db / scopeMaxGr * r.getHeight();
        g.setColour (Pal::accent.withAlpha (0.08f));
        g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        g.setColour (Pal::accent.withAlpha (0.4f));
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (r.getRight() + 2.0f, y - 6.0f, 20.0f, 12.0f), juce::Justification::centredLeft);
    }

    const int N = APP::scopeSize;
    const int w0 = proc.scopeWrite.load();
    const float dx = r.getWidth() / (N - 1);
    const float mid = r.getCentreY();
    juce::Path wave, gr;
    gr.startNewSubPath (r.getX(), r.getY());
    for (int i = 0; i < N; ++i)
    {
        const auto& col = proc.scope[(size_t) ((w0 + i) % N)];
        const float lvl = std::sqrt (juce::jlimit (0.0f, 1.0f, col.level.load()));
        const float x = r.getX() + i * dx;
        if (i == 0) wave.startNewSubPath (x, mid - lvl * r.getHeight() * 0.5f); else wave.lineTo (x, mid - lvl * r.getHeight() * 0.5f);
        gr.lineTo (x, r.getY() + juce::jlimit (0.0f, 1.0f, col.gr.load() / scopeMaxGr) * r.getHeight());
    }
    for (int i = N - 1; i >= 0; --i)
    {
        const auto& col = proc.scope[(size_t) ((w0 + i) % N)];
        const float lvl = std::sqrt (juce::jlimit (0.0f, 1.0f, col.level.load()));
        wave.lineTo (r.getX() + i * dx, mid + lvl * r.getHeight() * 0.5f);
    }
    wave.closeSubPath();
    g.setColour (Pal::level.withAlpha (0.14f));
    g.fillPath (wave);
    juce::Path fill (gr);
    fill.lineTo (r.getRight(), r.getY()); fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (Pal::accent.withAlpha (0.26f), 0, r.getY(), Pal::accent.withAlpha (0.03f), 0, r.getBottom(), false));
    g.fillPath (fill);
    glowPath (g, gr, Pal::accent, 1.6f);
}

// ── ProPanel ──────────────────────────────────────────────────────────────────
static void styleBox (juce::TextEditor& t, const juce::String& placeholder)
{
    t.setColour (juce::TextEditor::backgroundColourId, Pal::screen);
    t.setColour (juce::TextEditor::textColourId, Pal::accent);
    t.setColour (juce::TextEditor::outlineColourId, Pal::lampOff);
    t.setColour (juce::TextEditor::focusedOutlineColourId, Pal::accent);
    t.setColour (juce::CaretComponent::caretColourId, Pal::accent);
    t.setFont (Fonts::led (15.0f, 0.02f));
    t.setTextToShowWhenEmpty (placeholder, Pal::silkLo);
    t.setIndents (10, 10);
    t.setSelectAllWhenFocused (true);
}

ProPanel::ProPanel (AutoPressProAudioProcessor& p) : proc (p)
{
    styleBox (emailBox, "you@email.com");
    styleBox (keyBox, "99HZ-...");
    subscribe.setColour (juce::ToggleButton::textColourId, Pal::silkMid);
    subscribe.setColour (juce::ToggleButton::tickColourId, Pal::accent);
    subscribe.setColour (juce::ToggleButton::tickDisabledColourId, Pal::silkLo);
    for (auto* c : std::initializer_list<juce::Component*> { &emailBox, &keyBox, &subscribe, &trialButton, &unlockButton, &buyButton, &closeButton,
                                                             &activateButton, &moveButton })
        addAndMakeVisible (c);
    activateButton.getProperties().set ("lit", true);
    buyButton.getProperties().set ("lit", true);
    trialButton.getProperties().set ("lit", true);

    closeButton.onClick = [this] { setVisible (false); };
    buyButton.onClick = []
    {
        juce::URL ("https://99hertz.com/autopress-pro?utm_source=plugin&utm_medium=autopresspro&utm_campaign=upgrade").launchInDefaultBrowser();
    };
    // UNLOCK: a trial key is saved at once; a pro key activates this computer first (needs the internet once).
    unlockButton.onClick = [this]
    {
        busy = true; message = "ACTIVATING..."; messageIsError = false; refresh();
        juce::Component::SafePointer<ProPanel> safe (this);
        proc.licence.unlock (keyBox.getText(), [safe] (hz::LicenceInfo info, juce::String error)
        {
            if (safe == nullptr) return;
            if (error.isEmpty() && info.unlocked()) safe->keyBox.clear();
            safe->licenceResult (error, info.kind == hz::LicenceInfo::pro ? "UNLOCKED. THANK YOU!" : "TRIAL UNLOCKED");
        });
    };
    activateButton.onClick = [this]
    {
        busy = true; message = "ACTIVATING..."; messageIsError = false; refresh();
        juce::Component::SafePointer<ProPanel> safe (this);
        proc.licence.activate ([safe] (hz::LicenceInfo, juce::String error)
        {
            if (safe != nullptr) safe->licenceResult (error, "ACTIVATED. THANK YOU!");
        });
    };
    moveButton.onClick = [this]
    {
        busy = true; message = "MOVING..."; messageIsError = false; refresh();
        juce::Component::SafePointer<ProPanel> safe (this);
        proc.licence.deactivate ([safe] (hz::LicenceInfo, juce::String error)
        {
            if (safe != nullptr) safe->licenceResult (error, "DONE. PASTE YOUR KEY ON THE OTHER COMPUTER");
        });
    };
    trialButton.onClick = [this]
    {
        const auto email = emailBox.getText().trim();
        if (! email.containsChar ('@') || ! email.fromFirstOccurrenceOf ("@", false, false).containsChar ('.'))
        {
            message = "ENTER YOUR EMAIL TO START THE TRIAL"; messageIsError = true; refresh(); return;
        }
        busy = true; message = "STARTING YOUR TRIAL..."; messageIsError = false; refresh();
        juce::Component::SafePointer<ProPanel> safe (this);
        proc.licence.requestTrial (email, subscribe.getToggleState(), [safe] (hz::LicenceInfo info, juce::String error)
        {
            if (safe == nullptr) return;
            safe->busy = false;
            if (info.unlocked())
            {
                safe->proc.setLicenceInfo (info);
                safe->message = "UNLOCKED FOR 7 DAYS. YOUR KEY IS ON ITS WAY BY EMAIL.";
                safe->messageIsError = false;
                if (safe->onLicenceChanged) safe->onLicenceChanged();
            }
            else { safe->message = error.toUpperCase(); safe->messageIsError = true; }
            safe->refresh();
        });
    };
}

// After any licence request: re-read what's saved (the truth for this instance), then say how it went.
void ProPanel::licenceResult (const juce::String& error, const juce::String& okMessage)
{
    busy = false;
    proc.setLicenceInfo (proc.licence.check());
    message = error.isNotEmpty() ? error.toUpperCase() : okMessage;
    messageIsError = error.isNotEmpty();
    if (onLicenceChanged) onLicenceChanged();
    refresh();
}

void ProPanel::setMessage (const juce::String& text, bool isError)
{
    message = text; messageIsError = isError;
    refresh();
}

void ProPanel::licenceChanged()
{
    if (isVisible()) refresh();
}

void ProPanel::show (const juce::String& why)
{
    reason = why; message = {}; messageIsError = false;
    refresh();
    setVisible (true);
    toFront (true);
}

juce::Rectangle<int> ProPanel::card() const { return getLocalBounds().withSizeKeepingCentre (600, 456); }

void ProPanel::refresh()
{
    const auto k = proc.licenceInfo.kind;
    const bool canTrial = k == hz::LicenceInfo::none || k == hz::LicenceInfo::invalid;
    const bool isPro = k == hz::LicenceInfo::pro;
    const bool needsAct = k == hz::LicenceInfo::needsActivation;
    emailBox.setVisible (canTrial); subscribe.setVisible (canTrial); trialButton.setVisible (canTrial);
    keyBox.setVisible (! isPro && ! needsAct); unlockButton.setVisible (! isPro && ! needsAct); buyButton.setVisible (! isPro && ! needsAct);
    activateButton.setVisible (needsAct); moveButton.setVisible (isPro);
    for (auto* b : { &trialButton, &unlockButton, &activateButton, &moveButton }) b->setEnabled (! busy);
    getProperties().set ("hzMessage", message);
    trialButton.setButtonText (busy ? "STARTING..." : "START 7-DAY FREE TRIAL");
    resized();
    repaint();
}

void ProPanel::resized()
{
    const auto c = card();
    const int x = c.getX() + 36, w = c.getWidth() - 72;
    emailBox.setBounds (x, c.getY() + 222, w - 232, 38);
    trialButton.setBounds (x + w - 220, c.getY() + 218, 220, 46);
    subscribe.setBounds (x - 4, c.getY() + 264, 260, 24);
    keyBox.setBounds (x, c.getY() + 320, w - 232, 38);
    unlockButton.setBounds (x + w - 220, c.getY() + 316, 220, 46);
    buyButton.setBounds (x, c.getBottom() - 62, 220, 46);
    closeButton.setBounds (x + w - 120, c.getBottom() - 62, 120, 46);
    activateButton.setBounds (x, c.getY() + 218, 280, 46);
    moveButton.setBounds (x, c.getBottom() - 62, 270, 46);
}

void ProPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! card().contains (e.getPosition())) setVisible (false);
}

void ProPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.62f));
    const auto c = card().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (c.translated (0, 8.0f).expanded (4.0f), 12.0f);
    g.setGradientFill (juce::ColourGradient (Pal::panelTop.brighter (0.04f), 0, c.getY(), Pal::panelBot, 0, c.getBottom(), false));
    g.fillRoundedRectangle (c, 10.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (c.reduced (0.5f), 10.0f, 1.0f);

    const float x = c.getX() + 36.0f, w = c.getWidth() - 72.0f;
    g.setFont (Fonts::silkSemi (12.0f, 0.34f));
    g.setColour (juce::Colour (0xffa9adb3));
    g.drawText ("99HERTZ", juce::Rectangle<float> (x, c.getY() + 24.0f, w, 14.0f), juce::Justification::centredLeft);
    const auto titleFont = Fonts::silkBold (28.0f, 0.06f);
    g.setFont (titleFont);
    g.setColour (Pal::silk);
    g.drawText ("AUTOPRESS", juce::Rectangle<float> (x, c.getY() + 38.0f, w, 34.0f), juce::Justification::centredLeft);
    if (! proc.licenceInfo.isProKey())
    {
        g.setColour (Pal::accent);
        g.drawText ("UPGRADE", juce::Rectangle<float> (x + textWidth (titleFont, "AUTOPRESS") + 10.0f, c.getY() + 38.0f, 200.0f, 34.0f), juce::Justification::centredLeft);
    }

    const auto k = proc.licenceInfo.kind;
    juce::String sub = reason;
    if (k == hz::LicenceInfo::trial)             sub = "TRIAL: " + proc.licenceInfo.daysLeftText();
    else if (k == hz::LicenceInfo::trialExpired) sub = "YOUR FREE TRIAL HAS ENDED";
    else if (proc.licenceInfo.isProKey())          sub = proc.licenceInfo.licensedText();
    else if (sub.isEmpty())                      sub = "TRY EVERY UPGRADE FEATURE FREE FOR 7 DAYS";
    g.setFont (Fonts::led (14.0f, 0.08f));
    glowText (g, sub, { x, c.getY() + 78.0f, w, 18.0f }, juce::Justification::centredLeft, k == hz::LicenceInfo::trialExpired ? Pal::warn : Pal::accent);

    const char* feats[] = { "Punch, Smooth and Warm: three more compressor sounds",
                            "Set attack, release and ratio yourself",
                            "Sidechain filter: stop the kick pumping the bus",
                            "Drive: analogue-style warmth" };
    g.setFont (Fonts::silkMed (16.0f, 0.03f));
    for (int i = 0; i < 4; ++i)
    {
        const float y = c.getY() + 106.0f + i * 22.0f;
        g.setColour (Pal::accent);
        g.fillEllipse (x + 1.0f, y + 7.0f, 6.0f, 6.0f);
        g.setColour (Pal::silk);
        g.drawText (feats[i], juce::Rectangle<float> (x + 16.0f, y, w - 16.0f, 20.0f), juce::Justification::centredLeft);
    }

    g.setFont (Fonts::silkSemi (12.5f, 0.16f));
    g.setColour (Pal::silkMid);
    if (k == hz::LicenceInfo::none || k == hz::LicenceInfo::invalid)
        g.drawText ("NO CARD NEEDED. AFTER 7 DAYS IT GOES BACK TO FREE.", juce::Rectangle<float> (x, c.getY() + 200.0f, w, 14.0f), juce::Justification::centredLeft);
    if (k == hz::LicenceInfo::needsActivation)
        g.drawText ("YOUR KEY IS SAVED. ONE CLICK TO ACTIVATE THIS COMPUTER.", juce::Rectangle<float> (x, c.getY() + 200.0f, w, 14.0f), juce::Justification::centredLeft);
    if (! proc.licenceInfo.isProKey())
        g.drawText ("ALREADY BOUGHT IT? PASTE YOUR KEY", juce::Rectangle<float> (x, c.getY() + 302.0f, w, 14.0f), juce::Justification::centredLeft);

    if (message.isNotEmpty())
    {
        const bool longMsg = message.length() > 70;   // e.g. the server's "already in use on <computer>" message
        g.setFont (Fonts::led (longMsg ? 11.5f : 13.0f, 0.04f));
        g.setColour (messageIsError ? Pal::warn : Pal::accent);
        g.drawFittedText (message, juce::Rectangle<int> ((int) x, (int) c.getBottom() - (longMsg ? 93 : 86), (int) w, longMsg ? 30 : 18),
                          juce::Justification::centredLeft, longMsg ? 2 : 1);
    }
}

// ── Formatting ────────────────────────────────────────────────────────────────
static juce::String fmtMs (float ms)
{
    if (ms < 0.95f) return juce::String (ms, 1) + " MS";
    if (std::abs (ms - std::round (ms)) < 0.05f) return juce::String (juce::roundToInt (ms)) + " MS";
    return juce::String (ms, 1) + " MS";
}
static juce::String fmtRelMs (float ms)
{
    return juce::String (juce::roundToInt (ms)) + " MS";
}
static juce::String fmtRatio (float r)
{
    return (std::abs (r - std::round (r)) < 0.05f ? juce::String (juce::roundToInt (r)) : juce::String (r, 1)) + ":1";
}

// ── Editor ────────────────────────────────────────────────────────────────────
static juce::RangedAudioParameter& P (AutoPressProAudioProcessor& p, const char* id) { return *p.apvts.getParameter (id); }

static constexpr int W = 1000, H = 600, padX = 44;
static const char* typeNotes[] = { "TRANSPARENT", "SSL-STYLE BUS", "FET: BITE", "OPTO: SOFT", "VARI-MU: TUBE" };

AutoPressProAudioProcessorEditor::AutoPressProAudioProcessorEditor (AutoPressProAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      typeKeys (P (p, "type"), { "CLEAN", "GLUE", "PUNCH", "SMOOTH", "WARM" }),
      scopeView (p), proPanel (p)
{
    laf.reset (new HardwareLookAndFeel());
    setLookAndFeel (laf.get());

   #if JucePlugin_Build_Standalone
    // It's an effect: in the standalone app, pass the input through (JUCE mutes it by default).
    if (proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            holder->getMuteInputValue().setValue (false);
   #endif

    addAndMakeVisible (typeKeys);
    addAndMakeVisible (scopeView);
    typeKeys.allowValue = [this] (int v)
    {
        if (ap::gate::typeAllowed (v, proc.proUnlocked.load())) return true;
        openPro (juce::String (ap::typeName (v)) + " IS AN UPGRADE COMPRESSOR TYPE");
        return false;
    };

    learnButton.setTooltip ("Listen again and set everything to suit this audio");
    learnButton.onClick = [this] { proc.requestLearn(); };
    addAndMakeVisible (learnButton);

    licenceButton.getProperties().set ("pill", true);
    licenceButton.onClick = [this] { openPro ({}); };
    addAndMakeVisible (licenceButton);
    proPanel.onLicenceChanged = [this] { updateLocks(); repaint(); };

    addKnob ("threshold", "THRESHOLD");
    addKnob ("attack", "ATTACK", { ".1", ".3", "1", "3", "10", "30", "A" });
    addKnob ("release", "RELEASE", { "100", "300", "600", "1200", "A" });
    addKnob ("ratio", "RATIO", { "1.5", "2", "4", "10", "A" });
    addKnob ("schpf", "SC FILTER", { "OFF", "60", "90", "150" });
    addKnob ("drive", "DRIVE");
    addKnob ("mix", "MIX");
    knob ("attack")->slider.onLockedClick  = [this] { openPro ("SETTING THE ATTACK YOURSELF IS AN UPGRADE FEATURE"); };
    knob ("release")->slider.onLockedClick = [this] { openPro ("SETTING THE RELEASE YOURSELF IS AN UPGRADE FEATURE"); };
    knob ("ratio")->slider.onLockedClick   = [this] { openPro ("SETTING THE RATIO YOURSELF IS AN UPGRADE FEATURE"); };
    knob ("schpf")->slider.onLockedClick   = [this] { openPro ("THE SIDECHAIN FILTER IS AN UPGRADE FEATURE"); };
    knob ("drive")->slider.onLockedClick   = [this] { openPro ("DRIVE IS AN UPGRADE FEATURE"); };

    addChildComponent (proPanel);   // last, so it sits on top
    setSize (W, H);
    updateLocks();
    timerCallback();
    startTimerHz (30);
}

AutoPressProAudioProcessorEditor::~AutoPressProAudioProcessorEditor() { setLookAndFeel (nullptr); }

AutoPressProAudioProcessorEditor::Knob& AutoPressProAudioProcessorEditor::addKnob (const juce::String& id, const juce::String& label, juce::StringArray steps)
{
    auto* k = knobs.add (new Knob());
    k->label = label; k->paramId = id; k->stepLabels = steps;
    auto& s = k->slider;
    s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    s.setRotaryParameters (dialStart, dialEnd, true);
    auto* prm = proc.apvts.getParameter (id);
    s.setDoubleClickReturnValue (true, (double) prm->convertFrom0to1 (prm->getDefaultValue()));
    s.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    if (steps.size() > 1)
    {
        s.getProperties().set ("steps", steps.size());
        s.setMouseDragSensitivity (40 * steps.size());   // one step per ~40 px: feels like a detented switch
    }
    addAndMakeVisible (s);
    k->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, s);
    s.onValueChange = [this] { timerCallback(); };
    return *k;
}

AutoPressProAudioProcessorEditor::Knob* AutoPressProAudioProcessorEditor::knob (const juce::String& id)
{
    for (auto* k : knobs) if (k->paramId == id) return k;
    return nullptr;
}

void AutoPressProAudioProcessorEditor::resized()
{
    ledArea = { 300, 30, 420, 66 };
    learnButton.setBounds (W - padX - 120, 36, 120, 54);

    secType = { padX, 122, W - 2 * padX, 72 };
    typeKeys.setBounds (padX + 96, 124, W - 2 * padX - 96, 50);

    scopeView.setBounds (padX, 208, 600, 156);
    meterArea = { padX + 616, 208, W - 2 * padX - 616, 156 };

    const int top = 384, cy = 462, D = 64, cell = 130;
    secDyn = { padX, top, 4 * cell, 196 };
    secPro = { secDyn.getRight(), top, 2 * cell, 196 };
    secOut = { secPro.getRight(), top, W - padX - secPro.getRight(), 196 };
    auto place = [&] (const char* id, int x0, int w) { knob (id)->slider.setBounds (x0 + (w - D) / 2, cy - D / 2, D, D); };
    int i = 0;
    for (auto id : { "threshold", "attack", "release", "ratio" }) place (id, secDyn.getX() + cell * i++, cell);
    place ("schpf", secPro.getX(), cell);
    place ("drive", secPro.getX() + cell, cell);
    place ("mix", secOut.getX(), secOut.getWidth());

    proPanel.setBounds (getLocalBounds());
    renderBackground();
    updateLocks();
}

void AutoPressProAudioProcessorEditor::renderBackground()
{
    const float sc = 2.0f;
    background = juce::Image (juce::Image::ARGB, (int) (W * sc), (int) (H * sc), true);
    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (sc));

    auto panel = juce::Rectangle<float> (0, 0, (float) W, (float) H);
    g.setColour (juce::Colour (0xff0a0a0b)); g.fillAll();
    g.setGradientFill (juce::ColourGradient (Pal::panelTop, 0, 0, Pal::panelBot, 0, (float) H, false));
    g.fillRoundedRectangle (panel, 10.0f);
    g.setColour (juce::Colours::white.withAlpha (0.022f));
    for (int x = 0; x < W; x += 3) g.fillRect ((float) x, 0.0f, 1.0f, (float) H);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawLine (10.0f, 0.5f, W - 10.0f, 0.5f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawLine (10.0f, H - 1.0f, W - 10.0f, H - 1.0f, 2.0f);

    drawScrew (g, { 19.0f, 19.0f }, 0.49f);
    drawScrew (g, { W - 19.0f, 19.0f }, -0.91f);
    drawScrew (g, { 19.0f, H - 19.0f }, 1.29f);
    drawScrew (g, { W - 19.0f, H - 19.0f }, -0.21f);

    // Logo
    g.setFont (Fonts::silkSemi (12.0f, 0.34f));
    g.setColour (juce::Colour (0xffa9adb3));
    g.drawText ("99HERTZ", padX, 34, 240, 16, juce::Justification::centredLeft);
    const auto logo = Fonts::silkBold (31.0f, 0.06f);
    g.setFont (logo);
    g.setColour (Pal::silk);
    g.drawText ("AUTOPRESS", padX, 50, 240, 36, juce::Justification::centredLeft);

    // Dividers
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.fillRect ((float) padX, 110.0f, (float) (W - 2 * padX), 1.0f);
    g.fillRect ((float) padX, 374.0f, (float) (W - 2 * padX), 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    for (auto s : { secPro, secOut })
        g.fillRect ((float) s.getX() - 0.5f, (float) s.getY(), 1.0f, (float) s.getHeight() - 10.0f);

    // Type notes under the keys
    g.setFont (Fonts::silkMed (11.5f, 0.12f));
    g.setColour (Pal::silkLo);
    {
        const auto kb = typeKeys.getBounds();
        const int kw = (kb.getWidth() - 4 * 10) / 5;
        for (int i = 0; i < 5; ++i)
            g.drawText (typeNotes[i], kb.getX() + i * (kw + 10), kb.getBottom() + 3, kw, 14, juce::Justification::centred);
    }

    // Step legends around the stepped dials
    for (auto* k : knobs)
    {
        if (k->stepLabels.size() < 2) continue;
        const auto b = k->slider.getBounds().toFloat();
        const auto c = b.getCentre();
        const int n = k->stepLabels.size();
        g.setFont (Fonts::led (10.5f, 0.0f));
        for (int s = 0; s < n; ++s)
        {
            const float a = dialStart + s * (dialEnd - dialStart) / (n - 1);
            const float rr = b.getWidth() * 0.5f + 11.0f;
            const juce::Point<float> pt (c.x + std::sin (a) * rr, c.y - std::cos (a) * rr);
            const bool isAuto = k->stepLabels[s] == "A";
            g.setColour (isAuto ? Pal::accent.withAlpha (0.85f) : Pal::silkMid);
            g.drawText (k->stepLabels[s], juce::Rectangle<float> (pt.x - 16.0f, pt.y - 6.0f, 32.0f, 12.0f), juce::Justification::centred);
        }
    }
}

void AutoPressProAudioProcessorEditor::updateLocks()
{
    const bool pro = proc.proUnlocked.load();
    for (int t = 0; t < ap::numTypes; ++t) typeKeys.setLocked (t, ! ap::gate::typeAllowed (t, pro));
    auto lock = [&] (const char* id, bool proOnly)
    {
        auto* k = knob (id);
        const bool l = proOnly && ! pro;
        if (k->slider.locked != l)
        {
            k->slider.locked = l;
            k->slider.getProperties().set ("locked", l);
            k->slider.setMouseCursor (l ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::UpDownResizeCursor);
            k->slider.repaint();
        }
    };
    lock ("attack", ap::gate::proAttack);
    lock ("release", ap::gate::proRelease);
    lock ("ratio", ap::gate::proRatio);
    lock ("schpf", ap::gate::proScHpf);
    lock ("drive", ap::gate::proDrive);

    const auto& li = proc.licenceInfo;
    const auto t = li.buttonText();
    const bool warnCol = li.kind == hz::LicenceInfo::trialExpired || li.kind == hz::LicenceInfo::needsActivation;
    if (licenceButton.getButtonText() != t || (bool) licenceButton.getProperties()["warn"] != warnCol || licenceButton.getWidth() == 0)
    {
        licenceButton.setButtonText (t);
        licenceButton.getProperties().set ("warn", warnCol);
        const int w = (int) std::ceil (textWidth (Fonts::led (11.0f, 0.06f), t)) + 24;
        licenceButton.setBounds (padX, 88, w, 20);
        licenceButton.repaint();
    }
}

void AutoPressProAudioProcessorEditor::timerCallback()
{
    if (const int v = proc.licenceVersion.load(); v != seenLicenceVersion)
    {
        seenLicenceVersion = v;   // another instance unlocked, a trial ended, or the panel saved a key
        proPanel.licenceChanged();
        repaint();
    }
    const bool pro = proc.proUnlocked.load();
    const auto Ld = proc.getLearned();
    const auto e = proc.resolve (pro, Ld);
    updateLocks();
    typeKeys.showValue (e.type);
    {
        // A locked type saved in the project shows greyed on its key (CLEAN, which runs, stays lit).
        const int savedType = juce::jlimit (0, ap::numTypes - 1, (int) proc.apvts.getRawParameterValue ("type")->load());
        typeKeys.markSaved (ap::gate::typeAllowed (savedType, pro) ? -1 : savedType);
    }
    upgradeNudge = ! pro && proc.usesUpgradeSettings();
    learnButton.getProperties().set ("lit", proc.getPhase() != APP::idle);
    learnButton.repaint();

    // Dials: locked ones show what actually runs (AUTO / OFF / 0), or, greyed, an upgrade value saved in the project.
    auto setShown = [&] (const char* id, int idx, int steps)
    {
        auto* k = knob (id);
        if (k->slider.locked && ! upgradeNudge) k->slider.getProperties().set ("displayPos", (float) idx / (float) juce::jmax (1, steps - 1));
        else                                    k->slider.getProperties().remove ("displayPos");
    };
    setShown ("attack", e.attackAuto ? ap::attackAuto : (int) proc.apvts.getRawParameterValue ("attack")->load(), ap::numAttackSteps + 1);
    setShown ("release", e.releaseAuto ? ap::releaseAuto : (int) proc.apvts.getRawParameterValue ("release")->load(), ap::numReleaseSteps + 1);
    setShown ("ratio", e.ratioAuto ? ap::ratioAuto : (int) proc.apvts.getRawParameterValue ("ratio")->load(), ap::numRatioSteps + 1);
    setShown ("schpf", e.hpf, ap::numHpfSteps);
    {
        auto* k = knob ("drive");
        if (k->slider.locked && ! upgradeNudge) k->slider.getProperties().set ("displayPos", e.drive); else k->slider.getProperties().remove ("displayPos");
    }

    const bool learned = Ld.valid;
    knob ("threshold")->value = learned ? juce::String (e.threshDb, 1) + " DB" : "--";
    // Before the first learn AUTO has nothing to show yet.
    knob ("attack")->value  = e.attackAuto ? (learned ? "AUTO " + fmtMs (e.attackMs) : juce::String ("AUTO")) : fmtMs (e.attackMs);
    knob ("release")->value = e.releaseAuto ? (learned ? "AUTO " + fmtRelMs (e.releaseMs) : juce::String ("AUTO")) : fmtRelMs (e.releaseMs);
    knob ("ratio")->value   = e.ratioAuto ? (learned ? "AUTO " + fmtRatio (e.ratio) : juce::String ("AUTO")) : fmtRatio (e.ratio);
    if (e.type == ap::warm && (learned || ! e.ratioAuto)) knob ("ratio")->value = (e.ratioAuto ? "AUTO UP TO " : "UP TO ") + fmtRatio (e.ratio);
    knob ("schpf")->value   = e.hpf == 0 ? "OFF" : juce::String ((int) ap::hpfStepsHz[e.hpf]) + " HZ";
    knob ("drive")->value   = juce::String (juce::roundToInt (e.drive * 100.0f)) + " %";
    knob ("mix")->value     = juce::String (juce::roundToInt (e.mix * 100.0f)) + " %";
    if (upgradeNudge)   // locked dials holding a saved upgrade value: show that value (greyed)
        for (auto* id : { "attack", "release", "ratio", "schpf", "drive" })
            if (auto* k = knob (id); k->slider.locked)
                k->value = juce::String (id) == "drive"
                               ? juce::String (juce::roundToInt (proc.apvts.getRawParameterValue (id)->load())) + " %"
                               : proc.apvts.getParameter (id)->getCurrentValueAsText().toUpperCase();

    // LED
    const int ph = proc.getPhase();
    ledProgress = -1.0f;
    ledRightLabel = "RATIO"; ledRightText = learned ? fmtRatio (e.ratio) : "--";
    if (ph == APP::capturing && ! proc.hasHeardSignal())
    {
        ledLabel = learned ? "RE-LEARNING" : "LEARNING";
        ledText  = "PRESS PLAY";
        ledProgress = 0.0f;
    }
    else if (ph == APP::capturing)
    {
        ledLabel = learned ? "RE-LEARNING" : "LEARNING";
        ledProgress = proc.getLearnProgress();
        ledText = juce::String::fromUTF8 ("LISTENING\xe2\x80\xa6 ") + juce::String (juce::roundToInt (ledProgress * 100.0f)) + "%";
    }
    else if (ph == APP::analysing)
    {
        ledLabel = learned ? "RE-LEARNING" : "LEARNING";
        ledText = juce::String::fromUTF8 ("SETTING UP\xe2\x80\xa6");
        ledProgress = 1.0f;
    }
    else
    {
        ledLabel = juce::String (ap::materialName (Ld.material)) + juce::String::fromUTF8 (" \xc2\xb7 ") + ap::typeName (e.type);
        if (e.attackAuto && e.releaseAuto) ledText = "AUTO " + fmtMs (e.attackMs) + " / " + fmtRelMs (e.releaseMs);
        else ledText = juce::String (e.attackAuto ? "AUTO " : "") + fmtMs (e.attackMs) + " / " + (e.releaseAuto ? "AUTO " : "") + fmtRelMs (e.releaseMs);
    }

    ledWarn = false;
    if (upgradeNudge && ph == APP::idle)
    {
        ledWarn  = true;
        ledLabel = "THIS PROJECT USES UPGRADE SETTINGS";
        ledText  = "UPGRADE TO USE THEM AGAIN";
    }

    auto ballistic = [] (float& m, float v, float fall) { m = v > m ? v : m * fall + v * (1.0f - fall); };
    ballistic (mIn, proc.inPeak.load(), 0.86f);
    ballistic (mOut, proc.outPeak.load(), 0.86f);
    ballistic (mGr, proc.grNow.load(), 0.80f);

    scopeView.repaint();
    repaint (ledArea.expanded (12));
    repaint (meterArea.expanded (4));
    repaint (secDyn.getUnion (secPro).getUnion (secOut));
    repaint (secType.withWidth (96));
}

void AutoPressProAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.drawImage (background, getLocalBounds().toFloat());
    const bool pro = proc.proUnlocked.load();

    // ── LED display ──
    {
        auto r = ledArea.toFloat();
        drawScreen (g, r);
        auto in = r.reduced (20.0f, 11.0f);
        auto right = in.removeFromRight (76.0f);
        g.setFont (Fonts::led (12.0f, 0.14f));
        const auto ledCol = ledWarn ? Pal::warn : Pal::accent;
        glowText (g, ledLabel, in.removeFromTop (16.0f), juce::Justification::centredLeft, ledCol.withAlpha (0.75f));
        float fs = 21.0f;
        while (fs > 14.0f && textWidth (Fonts::led (fs, 0.04f), ledText) > in.getWidth() - 6.0f) fs -= 1.0f;
        g.setFont (Fonts::led (fs, 0.04f));
        glowText (g, ledText, in.withTrimmedBottom (ledProgress >= 0.0f ? 6.0f : 0.0f), juce::Justification::centredLeft, ledCol);
        if (ledProgress >= 0.0f)
        {
            auto bar = juce::Rectangle<float> (in.getX(), in.getBottom() - 3.0f, in.getWidth() - 10.0f, 3.0f);
            g.setColour (Pal::accent.withAlpha (0.12f)); g.fillRect (bar);
            g.setColour (Pal::accent); g.fillRect (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, ledProgress)));
        }
        g.setFont (Fonts::led (12.0f, 0.14f));
        glowText (g, ledRightLabel, right.removeFromTop (16.0f), juce::Justification::centredRight, Pal::accent.withAlpha (0.75f));
        g.setFont (Fonts::led (21.0f, 0.04f));
        glowText (g, ledRightText, right, juce::Justification::centredRight, Pal::accent);
    }

    // ── Meters: IN / GR / OUT ──
    {
        auto m = meterArea.toFloat();
        drawScreen (g, m);
        auto in = m.reduced (16.0f, 16.0f);
        const float rowH = in.getHeight() / 3.0f;
        struct Row { const char* name; float frac; juce::String val; bool gr; };
        const float inDb = juce::Decibels::gainToDecibels (mIn, -60.0f), outDb = juce::Decibels::gainToDecibels (mOut, -60.0f);
        auto dbText = [] (float db) { return db <= -59.0f ? juce::String ("-INF") : juce::String (juce::roundToInt (db)); };
        const Row rows[3] = { { "IN",  juce::jlimit (0.0f, 1.0f, (inDb + 48.0f) / 48.0f),  dbText (inDb), false },
                              { "GR",  juce::jlimit (0.0f, 1.0f, mGr / scopeMaxGr),       mGr < 0.05f ? juce::String ("0") : "-" + juce::String (mGr, 1), true },
                              { "OUT", juce::jlimit (0.0f, 1.0f, (outDb + 48.0f) / 48.0f), dbText (outDb), false } };
        for (int i = 0; i < 3; ++i)
        {
            auto row = juce::Rectangle<float> (in.getX(), in.getY() + i * rowH, in.getWidth(), rowH);
            g.setFont (Fonts::led (12.0f, 0.12f));
            glowText (g, rows[i].name, row.removeFromLeft (38.0f), juce::Justification::centredLeft, Pal::accent.withAlpha (0.75f));
            g.setFont (Fonts::led (14.0f, 0.04f));
            glowText (g, rows[i].val, row.removeFromRight (48.0f), juce::Justification::centredRight, Pal::accent);
            auto bar = row.reduced (4.0f, 0.0f).withSizeKeepingCentre (row.getWidth() - 8.0f, 12.0f);
            const int segs = 20; const float gap = 2.5f, sw = (bar.getWidth() - gap * (segs - 1)) / segs;
            const int lit = juce::roundToInt (rows[i].frac * segs);
            for (int s = 0; s < segs; ++s)
            {
                auto sr = juce::Rectangle<float> (bar.getX() + s * (sw + gap), bar.getY(), sw, bar.getHeight());
                const bool on = s < lit;
                auto col = Pal::accent;
                if (! rows[i].gr && s >= segs - 2) col = Pal::warn;   // last 6 dB before 0 dBFS
                if (on) { g.setColour (col.withAlpha (0.22f)); g.fillRect (sr.expanded (1.5f)); }
                g.setColour (on ? col : Pal::accent.withAlpha (0.08f));
                g.fillRect (sr);
            }
        }
    }

    // ── Section titles ──
    auto title = [&] (juce::Rectangle<int> s, const juce::String& t, bool on, int inset, bool lockedSection)
    {
        auto r = s.toFloat().withTrimmedLeft ((float) inset).withHeight (20.0f);
        if (on) { g.setColour (Pal::accent.withAlpha (0.3f)); g.fillEllipse (r.getX() - 2.5f, r.getCentreY() - 6.0f, 12.0f, 12.0f); }
        g.setColour (on ? Pal::accent : Pal::lampOff);
        g.fillEllipse (r.getX() + 1.0f, r.getCentreY() - 3.5f, 7.0f, 7.0f);
        const auto f = Fonts::silkBold (16.0f, 0.22f);
        g.setFont (f);
        g.setColour (Pal::silk);
        g.drawText (t, r.withTrimmedLeft (17.0f), juce::Justification::centredLeft);
        if (lockedSection)
            padlock (g, { r.getX() + 17.0f + textWidth (f, t) + 8.0f, r.getY() + 3.0f, 10.0f, 13.0f }, Pal::silkLo);
    };
    title ({ padX, 139, 96, 20 }, "TYPE", true, 0, false);
    title (secDyn, "DYNAMICS", true, 0, false);
    title (secPro, "EXTRAS", pro, 18, ! pro);
    title (secOut, "OUTPUT", true, 18, false);

    // ── Dial labels + values ──
    for (auto* k : knobs)
    {
        const auto sb = k->slider.getBounds();
        auto cell = juce::Rectangle<float> ((float) sb.getCentreX() - 62.0f, (float) sb.getBottom() + 12.0f, 124.0f, 16.0f);
        g.setFont (Fonts::silkSemi (13.0f, 0.16f));
        g.setColour (Pal::silk.withAlpha (k->slider.locked ? 0.6f : 1.0f));
        g.drawText (k->label, cell, juce::Justification::centred);
        g.setFont (Fonts::led (13.0f, 0.04f));
        g.setColour (Pal::accent.withAlpha (k->slider.locked ? 0.7f : 1.0f));
        g.drawText (k->value, cell.translated (0, 17.0f), juce::Justification::centred);
    }
    // Licensed: the licence email in small print beside the LICENSED pill.
    if (proc.licenceInfo.kind == hz::LicenceInfo::pro && proc.licenceInfo.email.isNotEmpty())
    {
        const auto lb = licenceButton.getBounds();
        g.setFont (Fonts::silkMed (12.0f, 0.04f));
        g.setColour (Pal::silkMid);
        g.drawFittedText (proc.licenceInfo.ownerText(), { lb.getRight() + 8, lb.getY(), ledArea.getX() - lb.getRight() - 16, lb.getHeight() },
                          juce::Justification::centredLeft, 1, 0.8f);
    }
    if (! pro)
    {
        g.setFont (Fonts::silkMed (12.0f, 0.06f));
        g.setColour (Pal::silkLo);
        g.drawText ("The free version runs AUTO. Click a padlock to take control.",
                    juce::Rectangle<int> (secDyn.getX(), secDyn.getBottom() - 22, secDyn.getWidth() + secPro.getWidth(), 16),
                    juce::Justification::centred);
    }
}
