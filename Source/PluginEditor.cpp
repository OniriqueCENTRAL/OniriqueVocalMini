#include "PluginEditor.h"
#include "OniriqueWindowSize.h"
#include <BinaryData.h>

using namespace onirique;

namespace
{
constexpr float kBaseW = ui::kEditorBaseW, kBaseH = ui::kEditorBaseH;
constexpr int kMinW = 760, kMaxW = 2000;
const char* kRowNames[4] { "EQ", "FX", "DELAY", "REVERB" };
const char* kRowSubs[4] { "TONE OF THE VOICE", "COLOUR & WIDTH", "AUX SEND", "AUX SENDS" };
} // namespace

OniriqueVocalMiniAudioProcessorEditor::OniriqueVocalMiniAudioProcessorEditor (OniriqueVocalMiniAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    tooltipWindow.setLookAndFeel (&lnf);
    setLookAndFeel (&lnf);   // menus et champs de texte dans le style du plugin
    setOpaque (true);
    logo = juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize);

    addAndMakeVisible (orb);
    addAndMakeVisible (presetBar);
    addAndMakeVisible (modeSel);
    for (auto* b : { &lowCut, &autoInput, &ping }) addAndMakeVisible (*b);
    lowCut.setTooltip ("Low cut at 90 Hz (18 dB/oct): removes rumble, pops and handling noise.");
    autoInput.setTooltip ("AUTO INPUT: the input gain sets itself, slowly, like an engineer on a fader, to keep the voice in the BEST "
                          "zone. Frozen in the silences: the noise between phrases never comes up.");
    ping.setTooltip ("The repeats bounce from one side to the other.");
    buttonAtts.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (st, ids::lowCut, lowCut));
    buttonAtts.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (st, ids::autoInput, autoInput));
    buttonAtts.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (st, ids::ping, ping));

    auto& chain = processor.getChain();
    deess.activity   = [&chain] { return chain.getDeEssReductionDb() / 6.0f; };
    delay.activity   = [&chain] { return chain.getDelayTail() * 6.0f; };
    rShort.activity  = [&chain] { return chain.getReverbTail (0) * 8.0f; };
    rSmall.activity  = [&chain] { return chain.getReverbTail (1) * 8.0f; };
    rBig.activity    = [&chain] { return chain.getReverbTail (2) * 8.0f; };
    rLush.activity   = [&chain] { return chain.getReverbTail (3) * 8.0f; };
    air.activity     = [&chain] { return 0.5f * chain.getAirGuard(); };

    for (auto* k : { &low, &mid, &hi, &air, &deess, &drive, &phone, &doubler, &chorus, &delay, &feed, &filter,
                     &rShort, &rSmall, &rBig, &rLush, &inK, &outK })
    {
        addAndMakeVisible (*k);
        animated.push_back (k);
    }
    addAndMakeVisible (time);
    addAndMakeVisible (inBar);
    addAndMakeVisible (outBar);
    for (auto* a : std::initializer_list<Animated*> { &orb, &presetBar, &modeSel, &lowCut, &autoInput, &ping, &time, &inBar, &outBar })
        animated.push_back (a);

    // redimensionnable, proportions verrouillées. La taille mémorisée est lue AVANT tout appel qui redimensionne.
    const int w = window::openingSize (processor.apvts.state, "ONIRIQUE VOCAL MINI",
                                       { 1000, juce::roundToInt (1000.0f * kBaseH / kBaseW) },
                                       { kMinW, juce::roundToInt ((float) kMinW * kBaseH / kBaseW) },
                                       { kMaxW, juce::roundToInt ((float) kMaxW * kBaseH / kBaseW) }).x;
    setResizable (true, true);
    setResizeLimits (kMinW, juce::roundToInt (kMinW * kBaseH / kBaseW), kMaxW, juce::roundToInt (kMaxW * kBaseH / kBaseW));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) (kBaseW / kBaseH));
    setSize (w, juce::roundToInt ((float) w * kBaseH / kBaseW));
    restoringSize = false;
    window::remember (processor.apvts.state, getWidth(), getHeight());

    // Licence : pastille et page d'activation, par-dessus tout le reste.
   #if ! ONIRIQUE_NO_LICENSE   // copie personnelle de l'équipe : ni pastille ni page d'activation
    licenseOverlay = std::make_unique<license::Overlay> ("ONIRIQUE VOCAL MINI", processor.license);
    addAndMakeVisible (*licenseOverlay);
   #endif
    resized();

   #if ONIRIQUE_UI_TEST
    // essais locaux uniquement (jamais dans les versions publiées) : captures et visuel de démo sans la page d'activation
    const auto test = juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_TEST", {});
    if (test.contains ("nolicense") && licenseOverlay != nullptr) licenseOverlay->setVisible (false);
    if (test.contains ("static")) orb.frozen = true;
   #endif

    startTimerHz (60);
}

OniriqueVocalMiniAudioProcessorEditor::~OniriqueVocalMiniAudioProcessorEditor()
{
    stopTimer();
    window::rememberOnThisComputer ("ONIRIQUE VOCAL MINI", getWidth(), getHeight());
    tooltipWindow.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void OniriqueVocalMiniAudioProcessorEditor::timerCallback()
{
    clock += 1.0 / 60.0;
    ++frame;
    for (auto* a : animated) a->tick();
    // le logo respire (30 images/s suffisent), la correction d'AUTO INPUT s'affiche quand elle change
    if ((frame & 1) == 0) repaint (logoRect.expanded (logoRect.getHeight() * 0.4f).getSmallestIntegerContainer());
    const float a = processor.getChain().getAutoInputDb();
    if (std::abs (a - autoShown) > 0.05f) { autoShown = a; repaint (autoRect.getSmallestIntegerContainer().expanded (2)); }
}

void OniriqueVocalMiniAudioProcessorEditor::resized()
{
    backdropLayer = {};
    const float u = scale();
    const float W = (float) getWidth();
    auto R = [u] (float x, float y, float w, float h) { return juce::Rectangle<float> (x * u, y * u, w * u, h * u).toNearestInt(); };

    // ---- en-tête
    logoRect = { 22.0f * u, 15.0f * u, 42.0f * u * 985.0f / 895.0f, 42.0f * u };
    modeSel.setBounds (R (262, 19, 132, 34));
    const float badgeW = licenseOverlay != nullptr ? (float) licenseOverlay->pillWidth() : 0.0f;
    const float badgeRight = W / u - 20.0f;
    if (licenseOverlay != nullptr)
        licenseOverlay->fit (getLocalBounds(), u, juce::Rectangle<float> (badgeRight - badgeW, 19.0f, badgeW, 34.0f).toNearestInt());
    presetBar.setBounds (juce::Rectangle<float> (290.0f * u, 34.0f * u).withRightX ((badgeRight - badgeW - 10.0f) * u).withY (19.0f * u).toNearestInt());

    // ---- l'orbe de la voix, à gauche
    orb.setBounds (R (14, 74, 384, 482));

    // ---- quatre rangées à droite : EQ (4), FX (5), DELAY (3 + TIME + PING-PONG), REVERB (4)
    const float x0 = 412.0f, x1 = 986.0f, y0 = 78.0f, rowH = 112.0f, gap = 8.0f;
    for (int r = 0; r < 4; ++r) rows[(size_t) r] = { x0 * u, (y0 + r * (rowH + gap)) * u, (x1 - x0) * u, rowH * u };
    auto place = [&] (int row, std::initializer_list<juce::Component*> items)
    {
        const float left = x0 + 118.0f, right = x1 - 12.0f, slot = (right - left) / 5.0f;
        int i = 0;
        for (auto* c : items)
        {
            const float cx = left + slot * ((float) i + 0.5f);
            const float y = y0 + row * (rowH + gap);
            if (dynamic_cast<MiniKnob*> (c) != nullptr) c->setBounds (R (cx - 40.0f, y + 10.0f, 80.0f, rowH - 16.0f));
            else c->setBounds (R (cx - 46.0f, y + 26.0f, 92.0f, 62.0f));
            ++i;
        }
    };
    place (0, { &low, &mid, &hi, &air });
    place (1, { &deess, &drive, &phone, &doubler, &chorus });
    place (2, { &delay, &feed, &filter, &time });
    place (3, { &rShort, &rSmall, &rBig, &rLush });
    {
        // LOW CUT dans la 5e place de la rangée EQ, PING-PONG dans celle du DELAY
        const float right = x1 - 12.0f, left = x0 + 118.0f, slot = (right - left) / 5.0f;
        lowCut.setBounds (R (left + slot * 4.5f - 50.0f, y0 + 38.0f, 100.0f, 30.0f));
        ping.setBounds (R (left + slot * 4.5f - 50.0f, y0 + 2 * (rowH + gap) + 38.0f, 100.0f, 30.0f));
    }

    // ---- IN et OUT en bas
    inK.setBounds (R (22, 562, 50, 52));
    inBar.setBounds (R (82, 570, 250, 30));
    autoInput.setBounds (R (346, 573, 118, 28));
    autoRect = juce::Rectangle<float> (472.0f * u, 573.0f * u, 110.0f * u, 28.0f * u);
    outBar.setBounds (R (634, 572, 280, 30));
    outK.setBounds (R (928, 562, 50, 52));

    if (logo.isValid())
    {
        const int lh = juce::jlimit (16, 600, juce::roundToInt (logoRect.getHeight() * 2.0f));
        logoScaled = logo.rescaled (juce::roundToInt ((float) lh * 985.0f / 895.0f), lh, juce::Graphics::highResamplingQuality);
        const int pad = lh / 4;
        juce::Image src (juce::Image::ARGB, logoScaled.getWidth() + 2 * pad, logoScaled.getHeight() + 2 * pad, true, juce::SoftwareImageType());
        { juce::Graphics lg (src); lg.drawImageAt (logoScaled, pad, pad); }
        logoGlow = juce::Image (juce::Image::ARGB, src.getWidth(), src.getHeight(), true, juce::SoftwareImageType());
        juce::ImageConvolutionKernel k (15);
        k.createGaussianBlur ((float) lh / 22.0f);
        k.applyToImage (logoGlow, src, src.getBounds());
    }

    if (! restoringSize)
        window::remember (processor.apvts.state, getWidth(), getHeight());   // taille mémorisée avec le projet
}

void OniriqueVocalMiniAudioProcessorEditor::paint (juce::Graphics& g)
{
    // fond : image prête, à la résolution réelle de l'écran (Retina : x2)
    const float ps = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (! backdropLayer.isValid() || std::abs (ps - layersScale) > 1.0e-3f)
    {
        layersScale = ps;
        backdropLayer = juce::Image (juce::Image::RGB, juce::jmax (1, juce::roundToInt ((float) getWidth() * ps)),
                                     juce::jmax (1, juce::roundToInt ((float) getHeight() * ps)), false, juce::SoftwareImageType());
        juce::Graphics bg (backdropLayer);
        bg.addTransform (juce::AffineTransform::scale (ps));
        paintBackdrop (bg);
    }
    g.drawImageTransformed (backdropLayer, juce::AffineTransform::scale (1.0f / layersScale));

    // logo qui respire
    if (logoScaled.isValid())
    {
        const float breathe = 0.5f + 0.5f * std::sin ((float) clock * 1.25f);
        const float pad = logoRect.getHeight() * 0.25f;
        g.setOpacity (0.35f + 0.45f * breathe);
        g.drawImage (logoGlow, logoRect.expanded (pad));
        g.setOpacity (1.0f);
        g.drawImage (logoScaled, logoRect);
    }

    // correction d'AUTO INPUT, sous le mètre d'entrée
    if (std::abs (autoShown) > 0.05f && autoShown < 90.0f)
    {
        const float u = scale();
        g.setColour (ui::haze);
        g.setFont (ui::font (ui::Face::mono, std::max (9.0f, 11.0f * u), false, 0.06f));
        g.drawText ("AUTO " + juce::String (autoShown > 0.0f ? "+" : "") + juce::String (autoShown, 1) + " dB", autoRect,
                    juce::Justification::centredLeft, false);
    }
}

// ---- le fond : nuit, halo de sang derrière l'orbe, cartes des rangées, titres (tout est fixe : dessiné une fois)
void OniriqueVocalMiniAudioProcessorEditor::paintBackdrop (juce::Graphics& g)
{
    const float u = scale();
    const float W = (float) getWidth(), H = (float) getHeight();
    g.fillAll (ui::night);
    {
        juce::ColourGradient grad (ui::deep2, 200.0f * u, 300.0f * u, ui::night, W * 0.9f, H, true);
        g.setGradientFill (grad);
        g.fillRect (0.0f, 0.0f, W, H);
    }
    // poussière d'étoiles très discrète
    juce::Random rnd (21);
    for (int i = 0; i < 70; ++i)
    {
        const float x = rnd.nextFloat() * W, y = rnd.nextFloat() * H, s = (0.6f + 1.4f * rnd.nextFloat() * rnd.nextFloat()) * u;
        g.setColour (ui::mist.withAlpha (0.05f + 0.10f * rnd.nextFloat()));
        g.fillEllipse (x, y, s, s);
    }

    // en-tête : titre et filet
    {
        const float x = logoRect.getRight() + 12.0f * u;
        g.setColour (ui::mist);
        const auto f1 = ui::font (ui::Face::display, 25.0f * u, true, 0.10f);
        g.setFont (f1);
        g.drawText ("VOCAL", juce::Rectangle<float> (x, 16.0f * u, 140.0f * u, 26.0f * u), juce::Justification::centredLeft, false);
        const float w1 = ui::textWidth (f1, "VOCAL");
        g.setColour (ui::ember);
        g.setFont (ui::font (ui::Face::display, 25.0f * u, false, 0.22f));
        g.drawText ("MINI", juce::Rectangle<float> (x + w1 + 8.0f * u, 16.0f * u, 120.0f * u, 26.0f * u), juce::Justification::centredLeft, false);
        g.setColour (ui::haze);
        g.setFont (ui::font (ui::Face::mono, 9.5f * u, false, 0.32f));
        g.drawText ("ONIRIQUE", juce::Rectangle<float> (x + 1.0f * u, 43.0f * u, 140.0f * u, 12.0f * u), juce::Justification::centredLeft, false);
        juce::ColourGradient line (ui::ember.withAlpha (0.0f), 0.0f, 0.0f, ui::ember.withAlpha (0.0f), W, 0.0f, false);
        line.addColour (0.35, ui::ember.withAlpha (0.45f));
        line.addColour (0.7, ui::line.withAlpha (0.25f));
        g.setGradientFill (line);
        g.fillRect (0.0f, 66.0f * u, W, 1.0f * u);
    }

    // rangées : verre sombre, filet, titre
    for (int r = 0; r < 4; ++r)
    {
        const auto row = rows[(size_t) r];
        const float rad = 14.0f * u;
        g.setColour (juce::Colours::black.withAlpha (0.30f));
        g.fillRoundedRectangle (row, rad);
        juce::ColourGradient sheen (ui::mist.withAlpha (0.045f), row.getX(), row.getY(), ui::mist.withAlpha (0.0f), row.getX(), row.getBottom(), false);
        g.setGradientFill (sheen);
        g.fillRoundedRectangle (row, rad);
        g.setColour (ui::line);
        g.drawRoundedRectangle (row.reduced (0.5f), rad, 1.0f * u);

        // titre à gauche, une lueur de braise derrière
        const auto titleR = juce::Rectangle<float> (row.getX() + 18.0f * u, row.getY(), 96.0f * u, row.getHeight());
        juce::ColourGradient glow (ui::ember.withAlpha (0.16f), titleR.getX() + 20.0f * u, titleR.getCentreY(),
                                   ui::ember.withAlpha (0.0f), titleR.getX() + 90.0f * u, titleR.getCentreY(), true);
        g.setGradientFill (glow);
        g.fillEllipse (titleR.withSizeKeepingCentre (titleR.getWidth() * 1.1f, titleR.getHeight() * 0.9f));
        g.setColour (ui::mist);
        g.setFont (ui::fitFont (ui::Face::display, 22.0f * u, titleR.getWidth() - 10.0f * u, kRowNames[r], true, 0.06f));
        g.drawText (kRowNames[r], titleR.withTrimmedBottom (row.getHeight() * 0.18f), juce::Justification::centredLeft, false);
        g.setColour (ui::haze.withAlpha (0.8f));
        g.setFont (ui::fitFont (ui::Face::mono, 8.5f * u, titleR.getWidth() - 4.0f * u, kRowSubs[r], false, 0.14f));
        g.drawText (kRowSubs[r], titleR.withTrimmedTop (row.getHeight() * 0.56f).withHeight (14.0f * u), juce::Justification::centredLeft, false);
        // séparateur vertical
        g.setColour (ui::line);
        g.fillRect (row.getX() + 114.0f * u, row.getY() + 16.0f * u, 1.0f * u, row.getHeight() - 32.0f * u);
    }

    // pied : filet et repères
    g.setColour (ui::line.withAlpha (0.10f));
    g.fillRect (14.0f * u, 556.0f * u, W - 28.0f * u, 1.0f * u);
}
