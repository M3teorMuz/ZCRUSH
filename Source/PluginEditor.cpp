#include "PluginEditor.h"
#include <BinaryData.h>

using namespace juce;

namespace
{
    const Colour kRed   (0xffe8201c);
    const Colour kHot   (0xffff2a1f);
    const Colour kBlood (0xffe01010);
    const Colour kLabel (0xffd2d2d4);

    // шрифты вшиты в плагин — выглядят одинаково на любом ПК
    Font monoFont (float h)
    {
        static Typeface::Ptr tf = Typeface::createSystemTypefaceFor (BinaryData::LiberationMonoBold_ttf,
                                                                      BinaryData::LiberationMonoBold_ttfSize);
        return Font (tf).withHeight (h);
    }
    Font sansFont (float h)
    {
        static Typeface::Ptr tf = Typeface::createSystemTypefaceFor (BinaryData::LiberationSansBold_ttf,
                                                                      BinaryData::LiberationSansBold_ttfSize);
        return Font (tf).withHeight (h);
    }

    void metalH (Graphics& g, Rectangle<float> r)   // цилиндр: горизонтальный градиент
    {
        ColourGradient gr (Colour (0xff6f6f74), r.getX(), 0, Colour (0xff4b4b50), r.getRight(), 0, false);
        gr.addColour (0.22, Colour (0xffeeeeef));
        gr.addColour (0.50, Colour (0xffa9a9ad));
        gr.addColour (0.80, Colour (0xff5d5d62));
        g.setGradientFill (gr);
        g.fillRect (r);
    }

    void metalV (Graphics& g, Rectangle<float> r)   // вертикальный градиент
    {
        ColourGradient gr (Colour (0xffe4e4e6), 0, r.getY(), Colour (0xff77777c), 0, r.getBottom(), false);
        gr.addColour (0.40, Colour (0xffb4b4b8));
        gr.addColour (0.52, Colour (0xff7d7d82));
        g.setGradientFill (gr);
        g.fillRect (r);
    }

    void chain (Graphics& g, float x, float y0, float y1)
    {
        g.setColour (Colour (0xffd4d4d7));
        bool flip = false;
        for (float y = y0; y < y1; y += 7.f, flip = ! flip)
            g.drawEllipse (x - (flip ? 2.6f : 1.6f), y, flip ? 5.2f : 3.2f, 9.f, 1.2f);
    }

    void rivet (Graphics& g, float x, float y, float r = 2.f)
    {
        g.setColour (Colour (0xff8a8a8e)); g.fillEllipse (x - r, y - r, r * 2, r * 2);
        g.setColour (Colour (0xff1a1a1c)); g.fillEllipse (x - r * 0.4f, y - r * 0.4f, r * 1.2f, r * 1.2f);
    }
}

//==============================================================================
void ZKnobLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h,
                                         float pos, float startA, float endA, Slider& s)
{
    const bool big = s.getComponentID() == "crush";
    const bool hot = big && s.getValue() >= 90.0;

    auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (1.f);
    const float R = jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto  c = b.getCentre();
    const float ang = startA + pos * (endA - startA);
    const float tw = big ? 6.f : 4.5f;
    const float ra = R - tw * 0.5f;

    Path track;
    track.addCentredArc (c.x, c.y, ra, ra, 0.f, startA, endA, true);
    g.setColour (hot ? Colour (0xff6b1210) : Colour (0xff8f8f93));
    g.strokePath (track, PathStrokeType (tw, PathStrokeType::curved, PathStrokeType::rounded));

    if (pos > 0.001f)
    {
        Path val;
        val.addCentredArc (c.x, c.y, ra, ra, 0.f, startA, ang, true);
        g.setColour (hot ? kHot : Colour (0xfff2f2f2));
        g.strokePath (val, PathStrokeType (tw, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // плоское светлое тело ручки
    const float rb = ra - tw * 0.5f - (big ? 6.f : 4.f);
    g.setColour (Colour (0xff1c1c1e));
    g.fillEllipse (c.x - rb - 1.5f, c.y - rb - 1.5f, rb * 2 + 3.f, rb * 2 + 3.f);
    ColourGradient grad (Colour (0xffe9e9e9), c.x, c.y - rb, Colour (0xffbdbdbd), c.x, c.y + rb, false);
    g.setGradientFill (grad);
    g.fillEllipse (c.x - rb, c.y - rb, rb * 2, rb * 2);
    g.setColour (Colour (0x33000000));
    g.drawEllipse (c.x - rb * 0.72f, c.y - rb * 0.72f, rb * 1.44f, rb * 1.44f, 1.f);

    // короткая метка
    Point<float> p0 (c.x + std::sin (ang) * rb * 0.42f, c.y - std::cos (ang) * rb * 0.42f);
    Point<float> p1 (c.x + std::sin (ang) * rb * 0.92f, c.y - std::cos (ang) * rb * 0.92f);
    g.setColour (hot ? kHot : Colour (0xff9a9a9e));
    g.drawLine (Line<float> (p0, p1), big ? 3.f : 2.4f);
}

//==============================================================================
ZCrushAudioProcessorEditor::ZCrushAudioProcessorEditor (ZCrushAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    auto setup = [this] (Slider& s, const String& id, double def)
    {
        s.setComponentID (id);
        s.setSliderStyle (Slider::RotaryVerticalDrag);
        s.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        s.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
        s.setDoubleClickReturnValue (true, def);
        s.setLookAndFeel (&lnf);
        s.onValueChange = [this] { repaint(); };
        addAndMakeVisible (s);
    };
    setup (crushS, "crush", 0.0);
    setup (bitsS,  "bits",  8.0);
    setup (rateS,  "rate",  4.0);
    setup (mixS,   "mix",   100.0);
    setup (outS,   "out",   0.0);

    aCrush = std::make_unique<Att> (proc.apvts, "crush", crushS);
    aBits  = std::make_unique<Att> (proc.apvts, "bits",  bitsS);
    aRate  = std::make_unique<Att> (proc.apvts, "rate",  rateS);
    aMix   = std::make_unique<Att> (proc.apvts, "mix",   mixS);
    aOut   = std::make_unique<Att> (proc.apvts, "out",   outS);

    Random rnd (1337);
    for (int i = 0; i < 34; ++i)
    {
        Blob b;
        if (i < 10) { b.x = 0.22f + rnd.nextFloat() * 0.56f; b.y = 0.80f + rnd.nextFloat() * 0.09f;
                      b.r = 1.5f + rnd.nextFloat() * 2.5f;  b.threshold = 0.02f + rnd.nextFloat() * 0.28f; b.drip = 0.f; }
        else        { b.x = 0.04f + rnd.nextFloat() * 0.92f; b.y = 0.06f + rnd.nextFloat() * 0.80f;
                      b.r = 4.f + rnd.nextFloat() * 10.f;    b.threshold = 0.30f + rnd.nextFloat() * 0.68f;
                      b.drip = rnd.nextFloat() < 0.55f ? 6.f + rnd.nextFloat() * 22.f : 0.f; }
        blobs.push_back (b);
    }

    setSize (420, 740);
    startTimerHz (30);
}

ZCrushAudioProcessorEditor::~ZCrushAudioProcessorEditor()
{
    stopTimer();
    for (auto* s : { &crushS, &bitsS, &rateS, &mixS, &outS })
        s->setLookAndFeel (nullptr);
}

void ZCrushAudioProcessorEditor::resized()
{
    crushS.setBounds (Rectangle<int> (82, 82).withCentre ({ 210, 542 }));
    const int xs[4] = { 66, 166, 266, 366 };
    Slider* small[4] = { &bitsS, &rateS, &mixS, &outS };
    for (int i = 0; i < 4; ++i)
        small[i]->setBounds (Rectangle<int> (56, 56).withCentre ({ xs[i], 662 }));
    buildBackground();
    buildStageStatic();
}

void ZCrushAudioProcessorEditor::mouseDown (const MouseEvent& e)
{
    if (bypassR.contains (e.getPosition()))
    {
        auto* bp = proc.apvts.getParameter ("bypass");
        bp->beginChangeGesture();
        bp->setValueNotifyingHost (bp->getValue() > 0.5f ? 0.f : 1.f);
        bp->endChangeGesture();
        repaint();
    }
}

void ZCrushAudioProcessorEditor::timerCallback()
{
    ++tick;
    const float target = (float) crushS.getValue() * 0.01f;
    anim += (target - anim) * 0.35f;
    lvl = jmax (proc.level.load(), lvl * 0.85f);
    repaint (stageR.expanded (4.f).toNearestInt());
    repaint (readR);
}

//==============================================================================
void ZCrushAudioProcessorEditor::buildBackground()
{
    background = Image (Image::ARGB, getWidth(), getHeight(), true);
    Graphics g (background);
    const float W = (float) getWidth(), H = (float) getHeight();

    g.setGradientFill (ColourGradient (Colour (0xff1b1b1d), 0, 0, Colour (0xff2f2f32), 0, H, false));
    g.fillAll();

    // нижняя панель с ручками (зернистая)
    g.setColour (Colour (0xff2e2e31)); g.fillRect (0.f, 484.f, W, H - 484.f);
    g.setColour (Colour (0xff0f0f10)); g.fillRect (0.f, 484.f, W, 2.f);

    Random rnd (7);
    for (int i = 0; i < 9000; ++i)
    {
        g.setColour ((rnd.nextFloat() < 0.5f ? Colours::white : Colours::black).withAlpha (0.04f + rnd.nextFloat() * 0.09f));
        g.fillRect (rnd.nextFloat() * W, rnd.nextFloat() * H, 1.3f, 1.3f);
    }

    // логотип
    auto logo = sansFont (38.f).withHorizontalScale (0.93f);
    g.setFont (logo);
    g.setColour (Colour (0xff4a0908)); g.drawText ("ZCRUSH", 21, 17, 230, 44, Justification::centredLeft);
    g.setColour (kRed);                g.drawText ("ZCRUSH", 18, 14, 230, 44, Justification::centredLeft);
    g.setColour (Colour (0x55ffffff)); g.fillRect (18.f, 24.f, 150.f, 1.f);   // «глитч»-полоса

    g.setColour (Colours::white); g.fillEllipse (20.f, 62.f, 6.f, 6.f);
    g.setColour (Colour (0xffe2e2e4));
    g.setFont (monoFont (9.f));
    g.drawText ("LAB UNIT 09 // SPECIMEN ON THE PLATE", 32, 58, 300, 14, Justification::centredLeft);

    // подписи
    g.setColour (kLabel);
    g.setFont (monoFont (10.f));
    g.drawText ("CRUSH", 160, 488, 100, 12, Justification::centred);
    const char* names[4] = { "BIT DEPTH", "SAMPLE RATE", "DRY/WET", "OUTPUT" };
    const int xs[4] = { 66, 166, 266, 366 };
    g.setFont (monoFont (8.f));
    for (int i = 0; i < 4; ++i)
        g.drawText (names[i], xs[i] - 46, 622, 92, 11, Justification::centred);

    g.setColour (Colour (0xffa9a9ad));
    g.setFont (monoFont (6.5f));
    g.drawText ("UNDERWATER PALACE  -  BITCRUSHER", 0, 724, (int) W, 10, Justification::centred);
}

void ZCrushAudioProcessorEditor::drawValueBox (Graphics& g, Rectangle<int> r, const String& t)
{
    g.setColour (Colour (0xff131315)); g.fillRect (r);
    g.setColour (Colour (0xffe6e6e6)); g.drawRect (r, 1);
    g.setColour (Colours::white);
    g.setFont (monoFont (9.f));
    g.drawText (t, r, Justification::centred);
}

void ZCrushAudioProcessorEditor::paint (Graphics& g)
{
    g.drawImageAt (background, 0, 0);

    // BYPASS
    const bool byp = proc.apvts.getRawParameterValue ("bypass")->load() > 0.5f;
    {
        auto r = bypassR.toFloat();
        g.setColour (byp ? kRed : Colour (0xff1c0e0e)); g.fillRoundedRectangle (r, 5.f);
        g.setColour (kRed); g.drawRoundedRectangle (r.reduced (1.f), 5.f, 1.6f);
        g.setColour (byp ? Colours::black : kRed);
        g.setFont (monoFont (11.f));
        g.drawText ("BYPASS", bypassR, Justification::centred);
    }

    drawStage (g);

    // панель значения (тонкая светлая обводка, как в референсе)
    {
        auto r = readR.toFloat();
        g.setColour (Colour (0xff18181a)); g.fillRoundedRectangle (r, 6.f);
        g.setColour (Colour (0xffd6d6d8)); g.drawRoundedRectangle (r.reduced (0.75f), 6.f, 1.2f);

        const float cv = (float) crushS.getValue();
        const bool hot = cv >= 90.f;
        g.setColour (Colour (0xffd2d2d4)); g.setFont (monoFont (7.f));
        g.drawText ("CRUSH", 24, 437, 60, 9, Justification::centredLeft);
        g.setColour (hot ? kHot : Colour (0xfff4f4f4));
        g.setFont (monoFont (26.f));
        g.drawText (String (cv, 1) + "%", 24, 445, 160, 28, Justification::centredLeft);

        const int nSeg = 20;
        const float sx = 196.f, sw = 8.6f, gap = 2.0f;
        const int lit = (int) std::round (anim * nSeg);
        for (int i = 0; i < nSeg; ++i)
        {
            const float t = (float) i / (nSeg - 1);
            const Colour off = Colour (0xff77777a).interpolatedWith (Colour (0xffb01818), jmin (1.f, t * 1.15f));
            g.setColour (i < lit ? Colour (0xfff3f3f3) : off.withAlpha (0.85f));
            g.fillRoundedRectangle (sx + i * (sw + gap), 447.f, sw, 18.f, 1.f);
        }
    }

    drawValueBox (g, Rectangle<int> (118, 16).withCentre ({ 210, 598 }), String (crushS.getValue(), 1) + " %");
    const int xs[4] = { 66, 166, 266, 366 };
    const String v[4] = { String (bitsS.getValue(), 1) + " bit", "1/" + String (rateS.getValue(), 1),
                          String (mixS.getValue(), 1) + " %",   String (outS.getValue(), 1) + " dB" };
    for (int i = 0; i < 4; ++i)
        drawValueBox (g, Rectangle<int> (76, 16).withCentre ({ xs[i], 702 }), v[i]);
}

//==============================================================================
void ZCrushAudioProcessorEditor::buildStageStatic()
{
    stageStatic = Image (Image::ARGB, (int) stageR.getWidth(), (int) stageR.getHeight(), true);
    Graphics g (stageStatic);
    drawStaticStage (g, Rectangle<float> (0.f, 0.f, stageR.getWidth(), stageR.getHeight()));
}

void ZCrushAudioProcessorEditor::drawSpeakerStatic (Graphics& g, Rectangle<float> r)
{
    g.setColour (Colour (0xff0b0b0c)); g.fillRoundedRectangle (r, 9.f);
    g.setColour (Colour (0xffe4e4e6)); g.drawRoundedRectangle (r.reduced (1.f), 9.f, 2.f);

    const float cx = r.getCentreX();
    const float ty = r.getY() + r.getHeight() * 0.17f;
    g.setColour (Colour (0xffe9e9ea)); g.drawEllipse (cx - 10.7f, ty - 10.7f, 21.4f, 21.4f, 2.f);
    g.setColour (Colour (0xff7d7d81)); g.fillEllipse (cx - 4.f, ty - 4.f, 8.f, 8.f);

    const float wy = r.getY() + r.getHeight() * 0.64f, wr = 33.f;
    g.setColour (Colour (0xff17171a)); g.fillEllipse (cx - wr, wy - wr, wr * 2, wr * 2);
    g.setColour (Colour (0xffeeeeef)); g.drawEllipse (cx - wr, wy - wr, wr * 2, wr * 2, 2.6f);
    g.setColour (Colour (0xff77777b));
    for (float k : { 0.86f, 0.70f })
        g.drawEllipse (cx - wr * k, wy - wr * k, wr * k * 2, wr * k * 2, 1.f);
    ColourGradient cone (Colour (0xffa4a4a8), cx - wr * 0.2f, wy - wr * 0.25f, Colour (0xff35353a), cx + wr * 0.55f, wy + wr * 0.55f, true);
    g.setGradientFill (cone);
    g.fillEllipse (cx - wr * 0.55f, wy - wr * 0.55f, wr * 1.1f, wr * 1.1f);
    g.setColour (Colour (0xff8c8c90)); g.drawEllipse (cx - wr * 0.4f, wy - wr * 0.4f, wr * 0.8f, wr * 0.8f, 1.f);
    g.setColour (Colour (0x66ffffff)); g.fillEllipse (cx - wr * 0.28f, wy - wr * 0.34f, wr * 0.26f, wr * 0.2f);

    const auto port = Rectangle<float> (cx - 21.f, r.getBottom() - 24.f, 42.f, 7.f);
    g.setColour (Colour (0xff1c1c1f)); g.fillRoundedRectangle (port, 3.5f);
    g.setColour (Colour (0xffe4e4e6)); g.drawRoundedRectangle (port, 3.5f, 1.3f);
}

void ZCrushAudioProcessorEditor::drawStaticStage (Graphics& g, Rectangle<float> frame)
{
    // двойная рамка: тонкая светлая линия снаружи и внутренняя сцена
    g.setColour (Colour (0xff161618)); g.fillRoundedRectangle (frame, 8.f);
    g.setColour (Colour (0xffa4a4a8)); g.drawRoundedRectangle (frame.reduced (0.75f), 8.f, 1.5f);

    const auto r = frame.reduced (6.f);
    const float W = r.getWidth(), H = r.getHeight();
    auto X = [&] (float f) { return r.getX() + W * f; };
    auto Y = [&] (float f) { return r.getY() + H * f; };

    Path clip; clip.addRoundedRectangle (r, 4.f);
    g.saveState();
    g.reduceClipRegion (clip);

    g.setColour (Colour (0xff1b1b1d)); g.fillRect (r);

    // задняя стена: тёмный верх, светлая «комната» ниже полки
    g.setColour (Colour (0xff2a2a2c)); g.fillRect (X (0.25f), r.getY(), W * 0.53f, H * 0.37f);
    g.setGradientFill (ColourGradient (Colour (0xff626265), 0, Y (0.37f), Colour (0xff39393c), 0, Y (0.92f), false));
    g.fillRect (X (0.25f), Y (0.37f), W * 0.53f, H * 0.55f);
    g.setColour (Colour (0xff9a9a9e)); g.fillRect (X (0.25f), Y (0.365f), W * 0.53f, 2.f);
    g.setColour (Colour (0xff111113));
    g.drawLine (X (0.25f), r.getY(), X (0.25f), Y (0.92f), 1.f);
    g.drawLine (X (0.78f), r.getY(), X (0.78f), Y (0.92f), 1.f);

    for (auto p : { Point<float> (0.03f, 0.12f), Point<float> (0.22f, 0.10f), Point<float> (0.80f, 0.08f),
                    Point<float> (0.95f, 0.20f), Point<float> (0.03f, 0.25f), Point<float> (0.80f, 0.55f) })
        { g.setColour (Colour (0xff77777b)); g.fillEllipse (X (p.x) - 1.5f, Y (p.y) - 1.5f, 3.f, 3.f); }

    // акустический поролон (пирамидки)
    for (auto f : { Point<float> (0.25f, 0.455f), Point<float> (0.568f, 0.773f) })
    {
        auto fr = Rectangle<float> (X (f.x), Y (0.20f), W * (f.y - f.x), H * 0.165f);
        g.setColour (Colour (0xff131315)); g.fillRect (fr);
        const float cs = 9.f;
        for (float yy = fr.getY(); yy < fr.getBottom() - 1.f; yy += cs)
            for (float xx = fr.getX(); xx < fr.getRight() - 1.f; xx += cs)
            {
                const float mx = xx + cs * 0.5f, my = yy + cs * 0.5f;
                Path t1; t1.addTriangle (xx, yy, xx + cs, yy, mx, my);           g.setColour (Colour (0xff5b5b61)); g.fillPath (t1);
                Path t2; t2.addTriangle (xx, yy, mx, my, xx, yy + cs);           g.setColour (Colour (0xff35353a)); g.fillPath (t2);
                Path t3; t3.addTriangle (xx + cs, yy, xx + cs, yy + cs, mx, my); g.setColour (Colour (0xff1d1d21)); g.fillPath (t3);
                Path t4; t4.addTriangle (xx, yy + cs, xx + cs, yy + cs, mx, my); g.setColour (Colour (0xff27272b)); g.fillPath (t4);
            }
    }

    // прожектор
    Path cone;
    cone.startNewSubPath (X (0.45f), Y (0.19f)); cone.lineTo (X (0.58f), Y (0.19f));
    cone.lineTo (X (0.74f), Y (0.80f));          cone.lineTo (X (0.28f), Y (0.80f)); cone.closeSubPath();
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.30f), 0, Y (0.19f), Colours::white.withAlpha (0.05f), 0, Y (0.80f), false));
    g.fillPath (cone);

    // левая труба с хомутом, цепь, рамка индикатора
    metalH (g, Rectangle<float> (X (0.058f), r.getY(), W * 0.046f, H * 0.31f));
    g.setColour (Colour (0xffb9b9bd)); g.fillRect (X (0.045f), Y (0.135f), W * 0.072f, 6.f);
    g.setColour (Colour (0xff4a4a4e)); g.drawRect (X (0.045f), Y (0.135f), W * 0.072f, 6.f, 1.f);
    chain (g, X (0.153f), r.getY(), Y (0.31f));
    {
        auto mb = Rectangle<float> (X (0.023f), Y (0.308f), W * 0.193f, H * 0.052f);
        g.setColour (Colour (0xff2c2c2f)); g.fillRoundedRectangle (mb, 2.f);
        g.setColour (Colour (0xffa9a9ad)); g.drawRoundedRectangle (mb, 2.f, 1.2f);
    }

    // правая цепь и корпус монитора с зелёным свечением
    chain (g, X (0.895f), Y (0.07f), Y (0.26f));
    {
        auto lcd = Rectangle<float> (X (0.824f), Y (0.26f), W * 0.157f, H * 0.136f);
        g.setGradientFill (ColourGradient (Colour (0x7a6bff3c), lcd.getCentreX(), lcd.getCentreY(), Colour (0x006bff3c), lcd.getCentreX() + 70.f, lcd.getCentreY(), true));
        g.fillEllipse (lcd.expanded (34.f));
        g.setColour (Colour (0xff0b130b)); g.fillRoundedRectangle (lcd, 3.f);
        g.setColour (Colour (0xffb4b4b8)); g.drawRoundedRectangle (lcd, 3.f, 1.8f);
    }

    // колонки
    drawSpeakerStatic (g, Rectangle<float> (X (0.006f), Y (0.353f), W * 0.233f, H * 0.545f));
    drawSpeakerStatic (g, Rectangle<float> (X (0.761f), Y (0.353f), W * 0.233f, H * 0.545f));

    // «земля» и основание с шевронами
    g.setColour (Colour (0xff0a0a0b)); g.fillRect (r.getX(), Y (0.90f), W, H * 0.10f);
    {
        auto br = Rectangle<float> (X (0.15f), Y (0.90f), W * 0.68f, H * 0.095f);
        g.setGradientFill (ColourGradient (Colour (0xffa0a09e), 0, br.getY(), Colour (0xff6f6f6d), 0, br.getBottom(), false));
        g.fillRect (br);
        g.setColour (Colour (0xfff2f2f0)); g.fillRect (br.getX(), br.getY(), br.getWidth(), 5.f);
        g.setColour (Colour (0xffe6e6e4));
        for (float xx = br.getX() + 14.f; xx < br.getRight() - 16.f; xx += 13.f)
        {
            Path ch; ch.startNewSubPath (xx, br.getBottom() - 5.f); ch.lineTo (xx + 5.f, br.getY() + 12.f); ch.lineTo (xx + 10.f, br.getBottom() - 5.f);
            g.strokePath (ch, PathStrokeType (1.1f));
        }
        rivet (g, br.getX() + 6.f, br.getY() + 6.f, 3.f); rivet (g, br.getRight() - 6.f, br.getY() + 6.f, 3.f);
    }

    // головка пресса: три кольца и болты
    for (int i = 0; i < 3; ++i)
        metalH (g, Rectangle<float> (X (0.369f), Y (0.027f) + i * H * 0.056f, W * 0.307f, H * 0.045f));
    for (auto p : { Point<float> (0.395f, 0.05f), Point<float> (0.65f, 0.05f), Point<float> (0.40f, 0.165f), Point<float> (0.645f, 0.165f) })
        rivet (g, X (p.x), Y (p.y), 2.f);

    g.restoreState();
    g.setColour (Colour (0x66000000)); g.drawRoundedRectangle (r, 4.f, 1.5f);
}

void ZCrushAudioProcessorEditor::drawCharacter (Graphics& g, float cx, float y0, float H, float c, float squash)
{
    // персонаж-«образец»: чёрная чёлка на пол-лица, один большой глаз, шитая улыбка,
    // худи с рваным подолом, длинные рукава. При сжатии — руки в стороны, глаза-«очки», рот открыт.
    const float U = 0.38f * H;
    const float ss = jlimit (0.f, 1.f, (c - 0.15f) / 0.35f);

    g.saveState();
    const float sx = jlimit (1.f, 1.8f, 1.f / std::sqrt (jmax (0.05f, squash)));
    g.addTransform (AffineTransform::scale (sx, squash, cx, y0));

    auto P = [&] (float dx, float dy) { return Point<float> (cx + dx * U, y0 - dy * U); };
    const Colour ink (0xff121214), hoodie (0xff3d3d40), sleeve (0xff4b4b4f),
                 pants (0xff5c5c5f), skin (0xfff3f3f3), hairC (0xff0d0d0f);

    // подставка
    g.setColour (Colour (0xffe9e9e9));
    g.fillRoundedRectangle (cx - 0.17f * U, y0 - 0.025f * U, 0.34f * U, 0.025f * U, 2.f);

    // штаны с рваным низом, шов, заплатка
    g.setColour (pants);
    g.fillRect (P (-0.16f, 0.32f).x, P (-0.16f, 0.32f).y, 0.32f * U, 0.28f * U);
    for (int i = 0; i < 8; ++i)
    {
        Path t; t.addTriangle (P (-0.16f + i * 0.04f, 0.04f).x, P (0, 0.04f).y,
                               P (-0.12f + i * 0.04f, 0.04f).x, P (0, 0.04f).y,
                               P (-0.14f + i * 0.04f, 0.0f).x,  P (0, 0.0f).y);
        g.fillPath (t);
    }
    g.setColour (ink); g.drawLine (Line<float> (P (0.f, 0.32f), P (0.f, 0.04f)), 1.5f);
    {
        Path patch;
        patch.startNewSubPath (P (-0.12f, 0.23f)); patch.lineTo (P (-0.05f, 0.24f));
        patch.lineTo (P (-0.06f, 0.17f));          patch.lineTo (P (-0.09f, 0.14f)); patch.closeSubPath();
        g.setColour (skin); g.fillPath (patch);
        g.setColour (Colour (0xff8b8b8f)); g.fillRect (P (0.07f, 0.22f).x, P (0.07f, 0.22f).y, 0.05f * U, 0.07f * U);
    }

    // худи: полосы, бахрома подола, цепочка
    g.setColour (hoodie);
    g.fillRect (P (-0.17f, 0.58f).x, P (-0.17f, 0.58f).y, 0.34f * U, 0.27f * U);
    g.setColour (Colour (0xff8d8d91));
    g.drawLine (Line<float> (P (-0.08f, 0.56f), P (-0.08f, 0.36f)), 1.2f);
    g.drawLine (Line<float> (P (0.10f, 0.56f),  P (0.10f, 0.36f)),  1.2f);
    g.setColour (Colour (0xffb4b4b7));
    for (float x = -0.16f; x < 0.16f; x += 0.025f)
        g.fillRect (P (x, 0.33f).x, P (x, 0.33f).y, 0.008f * U, 0.03f * U);
    g.setColour (skin);
    g.drawLine (Line<float> (P (0.f, 0.52f), P (0.f, 0.42f)), 3.f);
    g.drawLine (Line<float> (P (-0.04f, 0.47f), P (0.04f, 0.47f)), 2.f);

    // воротник капюшона
    g.setColour (Colour (0xff6a6a6e));
    g.fillEllipse (cx - 0.15f * U, P (0, 0.64f).y, 0.30f * U, 0.09f * U);

    // рукава и кисти
    for (int side = -1; side <= 1; side += 2)
    {
        const auto sh = P (side * 0.17f, 0.55f);
        const auto h0 = P (side * 0.27f, 0.31f), h1 = P (side * 0.52f, 0.22f);
        const Point<float> hd (h0.x + (h1.x - h0.x) * ss, h0.y + (h1.y - h0.y) * ss);
        g.setColour (ink);    g.drawLine (Line<float> (sh, hd), 0.125f * U + 2.f);
        g.setColour (sleeve); g.drawLine (Line<float> (sh, hd), 0.105f * U);
        g.setColour (Colour (0xffa0a0a4)); g.drawLine (Line<float> (sh, hd), 1.3f);
        g.setColour (skin);   g.fillEllipse (hd.x - 0.04f * U, hd.y - 0.04f * U, 0.08f * U, 0.08f * U);
        const float base = std::atan2 (hd.y - sh.y, hd.x - sh.x);
        for (int f = -1; f <= 1; ++f)
        {
            const float a = base + f * 0.5f;
            g.drawLine (Line<float> (hd, Point<float> (hd.x + std::cos (a) * 0.075f * U, hd.y + std::sin (a) * 0.075f * U)), 1.6f);
        }
    }

    // голова
    const float hw = 0.40f * U * (1.f + 0.12f * ss);
    g.setColour (skin);
    g.fillEllipse (cx - hw * 0.5f, y0 - 0.97f * U, hw, 0.38f * U);

    // волосы: купол, боковые пряди, чёлка-«молния» через пол-лица
    g.setColour (hairC);
    g.fillEllipse (cx - 0.235f * U, y0 - 1.04f * U, 0.47f * U, 0.34f * U);
    g.fillRect (P (-0.235f, 0.86f).x, P (-0.235f, 0.86f).y, 0.07f * U, 0.22f * U);
    g.fillRect (P (0.175f, 0.92f).x, P (0.175f, 0.92f).y, 0.055f * U, 0.20f * U);
    {
        Path fr;
        fr.startNewSubPath (P (-0.15f, 0.95f)); fr.lineTo (P (0.07f, 0.90f));
        fr.lineTo (P (-0.03f, 0.66f));          fr.lineTo (P (-0.10f, 0.78f)); fr.closeSubPath();
        g.fillPath (fr);
    }

    // глаза
    auto eye = [&] (Point<float> e, float r, float pr)
    {
        g.setColour (Colours::white); g.fillEllipse (e.x - r, e.y - r, r * 2, r * 2);
        g.setColour (ink);            g.drawEllipse (e.x - r, e.y - r, r * 2, r * 2, 1.6f);
        g.fillEllipse (e.x - pr, e.y - pr, pr * 2, pr * 2);
    };
    eye (P (0.08f, 0.77f), (0.058f + 0.016f * ss) * U, jmax (1.2f, 0.016f * U * (1.f - 0.5f * ss)));
    if (c > 0.5f)
        eye (P (-0.08f, 0.77f), 0.06f * U, jmax (1.2f, 0.01f * U));

    // румянец и серьга
    g.setColour (Colour (0x55e89a9a)); g.fillEllipse (P (0.10f, 0.71f).x, P (0.10f, 0.71f).y, 0.07f * U, 0.04f * U);
    g.setColour (Colour (0xffcfcfd2)); g.drawEllipse (P (0.20f, 0.77f).x - 0.02f * U, P (0.20f, 0.77f).y - 0.02f * U, 0.04f * U, 0.04f * U, 1.4f);

    // рот
    if (c < 0.5f)
    {
        const auto p0 = P (-0.11f, 0.66f), p1 = P (0.f, 0.60f), p2 = P (0.14f, 0.67f);
        Path m; m.startNewSubPath (p0); m.quadraticTo (p1, p2);
        g.setColour (ink); g.strokePath (m, PathStrokeType (1.5f));
        for (int i = 0; i <= 9; ++i)
        {
            const float t = i / 9.f, u = 1.f - t;
            const float bx = u * u * p0.x + 2 * u * t * p1.x + t * t * p2.x;
            const float by = u * u * p0.y + 2 * u * t * p1.y + t * t * p2.y;
            g.drawLine (bx, by - 0.018f * U, bx, by + 0.018f * U, 1.1f);
        }
    }
    else
    {
        g.setColour (ink);    g.fillEllipse (cx - 0.075f * U, y0 - 0.70f * U, 0.15f * U, 0.09f * U);
        g.setColour (kBlood); g.fillEllipse (cx - 0.045f * U, y0 - 0.65f * U, 0.09f * U, 0.05f * U);
        g.setColour (skin);   g.fillRect (cx - 0.03f * U, y0 - 0.71f * U, 0.025f * U, 0.025f * U);
        g.fillRect (cx + 0.005f * U, y0 - 0.71f * U, 0.025f * U, 0.025f * U);
    }

    g.restoreState();
}

void ZCrushAudioProcessorEditor::drawStage (Graphics& g)
{
    g.drawImageAt (stageStatic, (int) stageR.getX(), (int) stageR.getY());

    const auto r = sceneR;
    const float c = anim;
    const float W = r.getWidth(), H = r.getHeight();
    auto X = [&] (float f) { return r.getX() + W * f; };
    auto Y = [&] (float f) { return r.getY() + H * f; };

    g.saveState();
    Path clip; clip.addRoundedRectangle (r, 4.f);
    g.reduceClipRegion (clip);

    // индикатор слева: бегущая белая «шашка»
    {
        auto mb = Rectangle<float> (X (0.023f), Y (0.308f), W * 0.193f, H * 0.052f).reduced (3.f, 2.5f);
        const float cw = mb.getWidth() / 8.f;
        const int hot = jlimit (0, 7, (int) (lvl * 7.99f));
        for (int i = 0; i < 8; ++i)
        {
            g.setColour (i == hot ? Colour (0xfff4f4f4) : Colour (0xff808084));
            g.fillRect (mb.getX() + i * cw + 1.f, mb.getY(), cw - 2.f, mb.getHeight());
        }
    }

    // содержимое зелёного монитора
    {
        auto lcd = Rectangle<float> (X (0.824f), Y (0.26f), W * 0.157f, H * 0.136f).reduced (4.f);
        Random lr ((tick / 4) * 31 + 5);
        g.setColour (Colour (0xff53ff3a));
        for (int row = 0; row < 4; ++row)
        {
            float xx = lcd.getX() + lr.nextFloat() * 6.f;
            for (int k = 0; k < 2; ++k)
            {
                const float len = 5.f + lr.nextFloat() * (8.f + 8.f * lvl);
                if (xx + len < lcd.getRight())
                    g.fillRect (xx, lcd.getY() + 1.f + row * (lcd.getHeight() / 4.f), len, 2.4f);
                xx += len + 3.f;
            }
        }
        const float sx = lcd.getX() + std::fmod (tick * 1.5f, lcd.getWidth());
        g.setColour (Colour (0xffb8ffa8)); g.fillRect (sx, lcd.getY(), 1.6f, lcd.getHeight());
    }

    // шток и плита
    const float cx = X (0.5f);
    const float plateY = Y (0.42f + 0.39f * c);
    const float plateH = H * 0.085f;
    metalH (g, Rectangle<float> (cx - W * 0.034f, Y (0.19f), W * 0.068f, plateY - Y (0.19f)));

    // электрические дуги над плитой (три «горба», как в референсе)
    {
        Random er (tick / 2 * 13 + 3);
        const float amp = 9.f + 12.f * c;
        const float spans[3][2] = { { 0.28f, 0.40f }, { 0.42f, 0.47f }, { 0.55f, 0.72f } };
        for (auto& sp : spans)
        {
            Path z;
            const int n = 16;
            for (int i = 0; i <= n; ++i)
            {
                const float t = (float) i / n;
                const float yy = plateY - 3.f - std::sin (t * MathConstants<float>::pi) * amp * (0.55f + 0.45f * er.nextFloat());
                const float xx = X (sp[0]) + (X (sp[1]) - X (sp[0])) * t;
                if (i == 0) z.startNewSubPath (xx, yy); else z.lineTo (xx, yy);
            }
            g.setColour (Colours::white.withAlpha (0.22f)); g.strokePath (z, PathStrokeType (4.f));
            g.setColour (Colours::white.withAlpha (0.92f)); g.strokePath (z, PathStrokeType (1.5f));
        }
    }

    {
        auto pr = Rectangle<float> (X (0.235f), plateY, W * 0.51f, plateH);
        g.setColour (Colour (0x66000000)); g.fillRoundedRectangle (pr.translated (0.f, 3.f), 4.f);
        metalV (g, pr);
        auto band = Rectangle<float> (pr.getX(), pr.getY() + plateH * 0.58f, pr.getWidth(), plateH * 0.42f);
        g.setColour (Colour (0xfff0f0f0)); g.fillRect (band);
        g.saveState(); g.reduceClipRegion (band.toNearestInt());
        g.setColour (Colour (0xff101012));
        for (float xx = band.getX() - 12.f; xx < band.getRight(); xx += 12.f)
        {
            Path p; p.addQuadrilateral (xx, band.getBottom(), xx + 6.f, band.getBottom(), xx + 12.f, band.getY(), xx + 6.f, band.getY());
            g.fillPath (p);
        }
        g.restoreState();
        g.setColour (Colour (0xff2a2a2c)); g.drawRect (pr, 1.f);
    }

    // пульс и волны колонок
    for (int side = 0; side < 2; ++side)
    {
        const float scx = side == 0 ? X (0.006f + 0.1165f) : X (0.761f + 0.1165f);
        const float swy = Y (0.353f) + H * 0.545f * 0.64f;
        const float wr = 33.f * (1.f + 0.06f * lvl);
        g.setColour (Colours::white.withAlpha (0.35f * lvl));
        g.drawEllipse (scx - wr, swy - wr, wr * 2, wr * 2, 2.f);
        for (int k = 0; k < 2; ++k)
        {
            const float rr = wr + 9.f + k * 7.f;
            Path arc;
            const float a0 = side == 0 ? MathConstants<float>::pi * 0.28f : -MathConstants<float>::pi * 0.72f;
            arc.addCentredArc (scx, swy, rr, rr, 0.f, a0, a0 + MathConstants<float>::pi * 0.44f, true);
            g.setColour (Colours::white.withAlpha (jlimit (0.f, 1.f, 0.35f + 0.65f * lvl - k * 0.12f)));
            g.strokePath (arc, PathStrokeType (1.3f));
        }
    }

    // персонаж
    {
        const float baseTop = Y (0.90f);
        const float hN = 0.38f * H;
        const float avail = baseTop - (plateY + plateH);
        drawCharacter (g, cx, baseTop, H, c, jlimit (0.08f, 1.f, avail / hN));
    }

    // REC
    {
        auto rec = Rectangle<float> (X (0.70f), Y (0.07f), 46.f, 22.f);
        g.setColour (Colour (0xff0e0e10)); g.fillRoundedRectangle (rec, 5.f);
        g.setColour (Colour (0xffeaeaec)); g.drawRoundedRectangle (rec.reduced (0.7f), 5.f, 1.4f);
        g.setColour ((tick / 15) % 2 == 0 ? kHot : Colour (0xff7a1812));
        g.fillEllipse (rec.getX() + 7.f, rec.getCentreY() - 3.5f, 7.f, 7.f);
        g.setColour (Colours::white); g.setFont (monoFont (10.f));
        g.drawText ("REC", rec.withTrimmedLeft (15.f), Justification::centred);
    }

    // кровь
    if (c > 0.02f)
    {
        const float baseTop = Y (0.90f);
        for (auto& b : blobs)
        {
            if (c < b.threshold) continue;
            const float grow = jmin (1.f, (c - b.threshold) * 6.f + 0.35f);
            const float bx = X (b.x), by = Y (b.y), br = b.r * grow;
            g.setColour (kBlood);
            g.fillEllipse (bx - br, by - br, br * 2, br * 2);
            if (b.drip > 0.f)
                g.fillRoundedRectangle (bx - br * 0.28f, by, br * 0.56f, b.drip * grow, br * 0.28f);
            g.setColour (Colour (0x55ffffff));
            g.fillEllipse (bx - br * 0.55f, by - br * 0.55f, br * 0.45f, br * 0.45f);
        }
        g.setColour (kBlood);
        g.fillRect (X (0.19f), baseTop - 3.f, W * 0.62f * jmin (1.f, c * 1.6f), 3.f);
    }

    g.restoreState();
}
