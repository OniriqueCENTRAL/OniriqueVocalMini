#include "PluginEditor.h"
#include "OniriqueWindowSize.h"
#include <BinaryData.h>

using namespace onirique;

namespace
{
constexpr float kBaseW = ui::kEditorBaseW, kBaseH = ui::kEditorBaseH;
constexpr int kMinW = 840, kMaxW = 2240;
const char* kRowNames[5] { "CLEAN", "EQ", "FX", "DELAY", "REVERB" };
const char* kRowSubs[5] { "PLOSIVE / NOISE / GATE", "TONE OF THE VOICE", "COLOUR & WIDTH", "AUX SEND", "AUX SEND" };

// rangées à droite (base 1120 x 700) : x, largeur, hauteur de chaque rangée
constexpr float kRowX = 420.0f, kRowW = 686.0f, kRowY = 76.0f, kGap = 6.0f, kTitleW = 100.0f;
constexpr float kRowH[5] { 118.0f, 88.0f, 88.0f, 88.0f, 136.0f };

float rowTop (int r) { float y = kRowY; for (int i = 0; i < r; ++i) y += kRowH[i] + kGap; return y; }

juce::String dbShort (float db) { return (db > 0.05f ? "-" : "") + juce::String (std::abs (db), 1) + " dB"; }
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
    for (auto* b : { &bypassBtn, &lowCut, &autoInput, &ping, &plosOn, &nsOn, &gateOn }) addAndMakeVisible (*b);
    bypassBtn.setTooltip ("Bypass: hear the untouched voice to compare (click-free, aligned with the latency).");
    lowCut.setTooltip ("Low cut at 90 Hz (18 dB/oct): removes rumble and handling noise.");
    autoInput.setTooltip ("AUTO INPUT: the input gain sets itself, like an engineer on a preamp, to keep the voice in the BEST "
                          "zone. Frozen in the silences: the noise between phrases never comes up.");
    ping.setTooltip ("The repeats bounce from one side to the other.");
    plosOn.setTooltip ("DE-PLOSIVE on / off: removes the low-frequency pops of the P and B (and breath on the capsule), only "
                       "when they happen. Off: the signal is untouched.");
    nsOn.setTooltip ("NOISE SUPPRESSOR on / off: lowers steady background noise (hiss, fan, air conditioning, room), even under "
                     "the voice. It learns the noise by itself. Off: the signal is untouched.");
    gateOn.setTooltip ("NOISE GATE on / off: lowers the silences between phrases, never cuts word endings or soft breaths. "
                       "Placed before the compression and the effects: reverb and delay tails are never chopped.");
    const std::pair<const char*, juce::Button*> toggles[] { { ids::lowCut, &lowCut }, { ids::autoInput, &autoInput }, { ids::ping, &ping },
                                                           { ids::bypass, &bypassBtn }, { ids::plosOn, &plosOn }, { ids::nsOn, &nsOn },
                                                           { ids::gateOn, &gateOn } };
    for (const auto& [id, b] : toggles)
        buttonAtts.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (st, id, *b));

    auto& chain = processor.getChain();
    deess.activity   = [&chain] { return chain.getDeEssReductionDb() / 6.0f; };
    delay.activity   = [&chain] { return chain.getDelayTail() * 6.0f; };
    revMix.activity  = [&chain] { return chain.getReverbTail() * 8.0f; };
    revClean.activity = [&chain] { return chain.getReverb().getCleanAmount() * 3.0f; };
    air.activity     = [&chain] { return 0.5f * chain.getAirGuard(); };
    plosAmt.activity = [&chain] { return chain.getPlosiveReductionDb() / 12.0f; };
    nsAmt.activity   = [&chain] { return chain.getNoiseReductionDb() / 12.0f; };
    gateThr.activity = [&chain] { return chain.isGateOpen() ? 0.0f : 0.6f; };

    auto* plosP = st.getRawParameterValue (ids::plosOn);
    auto* nsP = st.getRawParameterValue (ids::nsOn);
    auto* gateP = st.getRawParameterValue (ids::gateOn);
    plosRead.level = [&chain, plosP] { return plosP->load() > 0.5f ? juce::jlimit (0.0f, 1.0f, chain.getPlosiveReductionDb() / 6.0f) : 0.0f; };
    plosRead.text = [&chain, plosP] { return plosP->load() > 0.5f ? dbShort (chain.getPlosiveReductionDb()) : juce::String ("OFF"); };
    nsRead.level = [&chain, nsP] { return nsP->load() > 0.5f ? juce::jlimit (0.0f, 1.0f, chain.getNoiseReductionDb() / 6.0f) : 0.0f; };
    nsRead.text = [&chain, nsP] { return nsP->load() > 0.5f ? dbShort (chain.getNoiseReductionDb()) : juce::String ("OFF"); };
    gateRead.colour = ui::mint;
    gateRead.level = [&chain, gateP] { return gateP->load() > 0.5f && chain.isGateOpen() ? 1.0f : 0.0f; };
    gateRead.text = [&chain, gateP]
    {
        if (gateP->load() <= 0.5f) return juce::String ("OFF");
        return chain.isGateOpen() ? juce::String ("OPEN") : "CLOSED " + dbShort (-chain.getGateGainDb());
    };
    plosRead.setTooltip ("De-plosive reduction right now (on the deep pop band).");
    nsRead.setTooltip ("Average noise reduction right now.");
    gateRead.setTooltip ("Gate state: green = open (voice), off = closed (the silence is lowered).");
    revMode.setTitle ("MODE");
    revScope.onClick = [this] { openPanel (1); };
    orb.bandsButton.onClick = [this] { openPanel (0); };
    bandPanel.close.onClick = [this] { openPanel (-1); };
    reverbPanel.close.onClick = [this] { openPanel (-1); };
    peqPanel.close.onClick = [this] { openPanel (-1); };

    // PARAMETRIC : ouvre l'EQ paramétrique en grand ; son témoin s'allume quand au moins une bande agit
    addAndMakeVisible (peqButton);
    animated.push_back (&peqButton);
    peqButton.setTooltip ("PARAMETRIC: an 8-band parametric EQ on a big screen, with the live voice, the curve of these knobs "
                          "and the total curve. The light is on when at least one band is active.");
    peqButton.onClick = [this] { openPanel (2); };
    peqButton.pillSize = true;   // exactement la taille de LOW CUT, juste au-dessus
    {
        std::array<std::atomic<float>*, 8> bandsOn {};
        for (int b = 0; b < 8; ++b) bandsOn[(size_t) b] = st.getRawParameterValue (ids::peq ("On", b));
        auto* power = st.getRawParameterValue (ids::peqPower);
        peqButton.lit = [bandsOn, power]
        {
            if (power->load() < 0.5f) return 0.0f;
            for (auto* b : bandsOn) if (b->load() > 0.5f) return 1.0f;
            return 0.0f;
        };
    }

    for (auto* k : { &plosAmt, &nsAmt, &gateThr, &gateRange, &gateRel, &low, &mid, &hi, &air, &deess, &drive, &phone, &doubler,
                     &chorus, &delay, &feed, &filter, &revMix, &revDecay, &revPre, &revClean, &inK, &outK })
    {
        addAndMakeVisible (*k);
        animated.push_back (k);
    }
    for (auto* c : std::initializer_list<juce::Component*> { &time, &inBar, &outBar, &plosRead, &nsRead, &gateRead, &revMode, &revScope })
        addAndMakeVisible (*c);
    for (auto* a : std::initializer_list<Animated*> { &orb, &presetBar, &modeSel, &bypassBtn, &lowCut, &autoInput, &ping, &plosOn,
                                                      &nsOn, &gateOn, &time, &inBar, &outBar, &plosRead, &nsRead, &gateRead,
                                                      &revMode, &revScope })
        animated.push_back (a);
    for (auto* pnl : panels) addChildComponent (*pnl);

    // redimensionnable, proportions verrouillées. La taille mémorisée est lue AVANT tout appel qui redimensionne.
    const int w = window::openingSize (processor.apvts.state, "ONIRIQUE VOCAL MINI",
                                       { 1120, juce::roundToInt (1120.0f * kBaseH / kBaseW) },
                                       { kMinW, juce::roundToInt ((float) kMinW * kBaseH / kBaseW) },
                                       { kMaxW, juce::roundToInt ((float) kMaxW * kBaseH / kBaseW) }).x;
    setResizable (true, true);
    setResizeLimits (kMinW, juce::roundToInt (kMinW * kBaseH / kBaseW), kMaxW, juce::roundToInt (kMaxW * kBaseH / kBaseW));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) (kBaseW / kBaseH));
    setSize (w, juce::roundToInt ((float) w * kBaseH / kBaseW));
    restoringSize = false;
    window::remember (processor.apvts.state, getWidth(), getHeight());

    // Licence : pastille et page d'activation, par-dessus tout le reste.
    licenseOverlay = std::make_unique<license::Overlay> ("ONIRIQUE VOCAL MINI", processor.license);
    addAndMakeVisible (*licenseOverlay);
    lastPreset = processor.presets.getCurrentName();
    resized();

   #if ONIRIQUE_UI_TEST
    // essais locaux uniquement (jamais dans les versions publiées) : captures et visuel de démo sans la page d'activation
    const auto test = juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_TEST", {});
    if (test.contains ("nolicense")) licenseOverlay->setVisible (false);
    if (test.contains ("static"))
    {
        orb.frozen = true; revScope.frozen = true; reverbPanel.scope.frozen = true; bandPanel.frozen = true; peqPanel.display.frozen = true;
    }
    if (test.contains ("bands")) openPanel (0);
    if (test.contains ("verb")) openPanel (1);
    if (test.contains ("peq")) openPanel (2);
    if (test.contains ("bands") || test.contains ("verb") || test.contains ("peq")) panelShown[(size_t) panelWanted] = 1.0f;
    if (test.contains ("eqtest"))
        juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("onirique-eq-selftest.txt")
            .replaceWithText (peqPanel.runSelfTest());
    if (test.contains ("eqdemo"))
    {
        // une correction de voix typique, pour la capture : coupe de grave, boîte creusée, résonance dynamique, air
        auto set = [this] (const juce::String& id, float v)
        { if (auto* prm = st.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v)); };
        set (ids::peq ("On", 0), 1); set (ids::peq ("Freq", 0), 95.0f); set (ids::peq ("Slope", 0), 1);
        set (ids::peq ("On", 2), 1); set (ids::peq ("Freq", 2), 340.0f); set (ids::peq ("Gain", 2), -3.5f); set (ids::peq ("Q", 2), 1.4f);
        set (ids::peq ("On", 4), 1); set (ids::peq ("Freq", 4), 3150.0f); set (ids::peq ("Gain", 4), -5.0f); set (ids::peq ("Q", 4), 3.2f);
        set (ids::peq ("Dyn", 4), 1); set (ids::peq ("Thresh", 4), -34.0f);
        set (ids::peq ("On", 6), 1); set (ids::peq ("Freq", 6), 11000.0f); set (ids::peq ("Gain", 6), 2.5f);
        peqPanel.selectBand (4);
    }
    if (test.contains ("lit"))
    {
        // témoins et halos allumés sans audio (contrôle des halos près des bords)
        plosRead.level = [] { return 1.0f; }; plosRead.text = [] { return juce::String ("-6.2 dB"); };
        nsRead.level = [] { return 1.0f; };   nsRead.text = [] { return juce::String ("-14.9 dB"); };
        gateRead.level = [] { return 1.0f; }; gateRead.text = [] { return juce::String ("OPEN"); };
        for (auto* k : { &plosAmt, &nsAmt, &gateThr, &revMix, &revClean }) k->activity = [] { return 1.0f; };
    }
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

juce::Rectangle<int> OniriqueVocalMiniAudioProcessorEditor::panelArea (int which) const
{
    const float u = scale();
    const float bottom = rowTop (4) + kRowH[4];
    // PARAMETRIC : toute la largeur, exactement par-dessus la colonne du COMP et les rangées ; l'en-tête et IN / OUT restent visibles
    if (which == 2) return juce::Rectangle<float> (14.0f * u, 70.0f * u, (kRowX + kRowW - 14.0f) * u, 552.0f * u).toNearestInt();
    return juce::Rectangle<float> (kRowX * u, kRowY * u, kRowW * u, (bottom - kRowY) * u).toNearestInt();
}

void OniriqueVocalMiniAudioProcessorEditor::openPanel (int which)
{
    panelWanted = which;
    for (int i = 0; i < (int) panels.size(); ++i)
        if (i == which) { panels[(size_t) i]->setVisible (true); panels[(size_t) i]->toFront (false); }
    if (licenseOverlay != nullptr) licenseOverlay->toFront (false);
}

void OniriqueVocalMiniAudioProcessorEditor::timerCallback()
{
    clock += 1.0 / 60.0;
    ++frame;
    for (auto* a : animated) a->tick();

    // panneaux : glissent et apparaissent en fondu (ressort doux), les rangées restent dessous
    for (int i = 0; i < (int) panels.size(); ++i)
    {
        auto* pnl = panels[(size_t) i];
        const float target = panelWanted == i ? 1.0f : 0.0f;
        float& s = panelShown[(size_t) i];
        if (ui::approach (s, target, 0.22f) || pnl->isVisible())
        {
            const auto area = panelArea (i);
            const float u = scale();
            const float ease = 1.0f - (1.0f - s) * (1.0f - s);
            pnl->setBounds (area.translated (0, juce::roundToInt ((1.0f - ease) * 18.0f * u)));
            pnl->setAlpha (ease);
            if (s <= 0.001f && target == 0.0f) pnl->setVisible (false);
        }
        if (pnl->isVisible()) dynamic_cast<Animated*> (pnl)->tick();
    }

    // le logo respire (30 images/s suffisent), la correction d'AUTO INPUT s'affiche quand elle change
    if ((frame & 1) == 0) repaint (logoRect.expanded (logoRect.getHeight() * 0.4f).getSmallestIntegerContainer());
    const float a = processor.getChain().getAutoInputDb();
    if (std::abs (a - autoShown) > 0.05f) { autoShown = a; repaint (autoRect.getSmallestIntegerContainer().expanded (2)); }

    // nouveau preset : une vague de lumière descend sur les rangées
    const auto name = processor.presets.getCurrentName();
    if (name != lastPreset) { lastPreset = name; sweep = 1.0f; }
    if (sweep > 0.0f)
    {
        sweep = std::max (0.0f, sweep - 1.0f / 30.0f);
        const float u = scale();
        repaint (juce::Rectangle<float> (0.0f, 70.0f * u, (float) getWidth(), (rowTop (4) + kRowH[4] - 64.0f) * u).toNearestInt());
    }
}

void OniriqueVocalMiniAudioProcessorEditor::resized()
{
    backdropLayer = {};
    const float u = scale();
    const float W = (float) getWidth();
    auto R = [u] (float x, float y, float w, float h) { return juce::Rectangle<float> (x * u, y * u, w * u, h * u).toNearestInt(); };

    // ---- en-tête
    logoRect = { 22.0f * u, 14.0f * u, 42.0f * u * 985.0f / 895.0f, 42.0f * u };
    modeSel.setBounds (R (352, 19, 132, 32));
    bypassBtn.setBounds (R (494, 20, 104, 30));
    const float badgeW = licenseOverlay != nullptr ? (float) licenseOverlay->pillWidth() : 0.0f;
    const float badgeRight = W / u - 18.0f;
    if (licenseOverlay != nullptr)
        licenseOverlay->fit (getLocalBounds(), u, juce::Rectangle<float> (badgeRight - badgeW, 19.0f, badgeW, 32.0f).toNearestInt());
    presetBar.setBounds (juce::Rectangle<float> (300.0f * u, 34.0f * u).withRightX ((badgeRight - badgeW - 10.0f) * u).withY (18.0f * u).toNearestInt());

    // ---- COMP et MULTIBAND, à gauche
    orb.setBounds (R (14, 70, 392, 552));

    // ---- rangées à droite
    for (int r = 0; r < 5; ++r) rows[(size_t) r] = { kRowX * u, rowTop (r) * u, kRowW * u, kRowH[r] * u };
    const float left = kRowX + kTitleW, right = kRowX + kRowW - 10.0f, slot = (right - left) / 5.0f;
    auto place = [&] (int row, std::initializer_list<juce::Component*> items, float knobTop = 4.0f, float knobH = 82.0f)
    {
        int i = 0;
        for (auto* c : items)
        {
            const float cx = left + slot * ((float) i + 0.5f), y = rowTop (row);
            if (c == nullptr) { ++i; continue; }
            if (dynamic_cast<MiniKnob*> (c) != nullptr) c->setBounds (R (cx - 42.0f, y + knobTop, 84.0f, knobH));
            else c->setBounds (R (cx - 48.0f, y + knobTop + 14.0f, 96.0f, 60.0f));
            ++i;
        }
    };

    // CLEAN : PLOSIVE (interrupteur, réduction, AMOUNT) | NOISE (idem) | GATE (interrupteur, état, THRESH, RANGE, RELEASE)
    {
        const float y = rowTop (0);
        const float gx[3] { left + 4.0f, left + 116.0f, left + 232.0f };
        plosOn.setBounds (R (gx[0], y + 8.0f, 100, 26));
        plosRead.setBounds (R (gx[0] + 6.0f, y + 36.0f, 94, 14));
        plosAmt.setBounds (R (gx[0] + 12.0f, y + 50.0f, 76, 66));
        nsOn.setBounds (R (gx[1], y + 8.0f, 100, 26));
        nsRead.setBounds (R (gx[1] + 6.0f, y + 36.0f, 94, 14));
        nsAmt.setBounds (R (gx[1] + 12.0f, y + 50.0f, 76, 66));
        gateOn.setBounds (R (gx[2], y + 8.0f, 100, 26));
        gateRead.setBounds (R (gx[2] + 112.0f, y + 14.0f, 200, 14));
        const float gw = (right - gx[2]) / 3.0f;
        for (int k = 0; k < 3; ++k)
            (k == 0 ? gateThr : k == 1 ? gateRange : gateRel).setBounds (R (gx[2] + gw * (float) k + gw * 0.5f - 40.0f, y + 40.0f, 80, 76));
    }
    place (1, { &low, &mid, &hi, &air, nullptr });
    place (2, { &deess, &drive, &phone, &doubler, &chorus });
    place (3, { &delay, &feed, &filter, &time, nullptr });
    // EQ : LOW CUT, et juste en dessous PARAMETRIC qui ouvre l'EQ paramétrique en grand
    lowCut.setBounds (R (left + slot * 4.5f - 54.0f, rowTop (1) + 12.0f, 108, 28));
    peqButton.setBounds (R (left + slot * 4.5f - 54.0f, rowTop (1) + 48.0f, 108, 28));
    ping.setBounds (R (left + slot * 4.5f - 50.0f, rowTop (3) + 30.0f, 100, 28));
    // REVERB : le mode au-dessus, MIX / DECAY / PRE-DELAY / CLEAN, la visualisation (ouvre le panneau)
    revMode.setBounds (R (left + 6.0f, rowTop (4) + 8.0f, right - left - 12.0f, 28));
    place (4, { &revMix, &revDecay, &revPre, &revClean, nullptr }, 44.0f, 86.0f);
    revScope.setBounds (R (left + slot * 4.0f + 6.0f, rowTop (4) + 44.0f, slot - 10.0f, 84.0f));

    // ---- IN et OUT en bas
    const float fy = rowTop (4) + kRowH[4] + 10.0f;
    inK.setBounds (R (22, fy, 52, 58));
    inBar.setBounds (R (84, fy + 12.0f, 270, 32));
    autoInput.setBounds (R (368, fy + 16.0f, 118, 28));
    autoRect = juce::Rectangle<float> (496.0f * u, (fy + 16.0f) * u, 120.0f * u, 28.0f * u);
    outBar.setBounds (R (744, fy + 12.0f, 290, 32));
    outK.setBounds (R (1046, fy, 52, 58));

    for (int i = 0; i < (int) panels.size(); ++i)
        if (panels[(size_t) i]->isVisible())
        {
            const float ease = 1.0f - (1.0f - panelShown[(size_t) i]) * (1.0f - panelShown[(size_t) i]);
            panels[(size_t) i]->setBounds (panelArea (i).translated (0, juce::roundToInt ((1.0f - ease) * 18.0f * u)));
        }
        else panels[(size_t) i]->setBounds (panelArea (i));

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
        g.setOpacity (0.30f + 0.40f * breathe);
        g.drawImage (logoGlow, logoRect.expanded (pad));
        g.setOpacity (1.0f);
        g.drawImage (logoScaled, logoRect);
    }

    // correction d'AUTO INPUT, à côté de l'interrupteur
    if (std::abs (autoShown) > 0.05f && autoShown < 90.0f)
    {
        const float u = scale();
        g.setColour (ui::haze);
        g.setFont (ui::font (ui::Face::mono, std::max (9.0f, 10.5f * u), true, 0.06f));
        g.drawText ("AUTO " + juce::String (autoShown > 0.0f ? "+" : "") + juce::String (autoShown, 1) + " dB", autoRect,
                    juce::Justification::centredLeft, false);
    }
}

void OniriqueVocalMiniAudioProcessorEditor::paintOverChildren (juce::Graphics& g)
{
    if (sweep <= 0.0f) return;
    // changement de preset : une bande de lumière rouge descend sur les rangées et s'efface
    const float u = scale();
    const float t = 1.0f - sweep;
    const float top = 70.0f * u, bottom = (rowTop (4) + kRowH[4]) * u;
    const float y = top + (bottom - top) * (1.0f - (1.0f - t) * (1.0f - t));
    const float h = 90.0f * u, alpha = 0.16f * std::sin (sweep * juce::MathConstants<float>::pi);
    juce::ColourGradient grad (ui::ember.withAlpha (0.0f), 0.0f, y - h, ui::ember.withAlpha (0.0f), 0.0f, y + h * 0.3f, false);
    grad.addColour (0.75, ui::ember.withAlpha (alpha));
    grad.addColour (0.92, ui::bone.withAlpha (alpha * 0.6f));
    g.setGradientFill (grad);
    g.fillRect (juce::Rectangle<float> (0.0f, y - h, (float) getWidth(), h * 1.3f));
}

// ---- le fond : noir profond, lueurs rouges très discrètes, cartes de verre des rangées, titres (dessiné une fois)
void OniriqueVocalMiniAudioProcessorEditor::paintBackdrop (juce::Graphics& g)
{
    const float u = scale();
    const float W = (float) getWidth(), H = (float) getHeight();
    g.fillAll (ui::night);
    {
        juce::ColourGradient grad (ui::blood.withAlpha (0.55f), 210.0f * u, 300.0f * u, ui::night.withAlpha (0.0f), 210.0f * u + W * 0.55f, 300.0f * u, true);
        g.setGradientFill (grad);
        g.fillRect (0.0f, 0.0f, W, H);
        juce::ColourGradient grad2 (ui::blood.withAlpha (0.22f), W * 0.92f, H * 1.05f, ui::night.withAlpha (0.0f), W * 0.92f - W * 0.45f, H * 1.05f, true);
        g.setGradientFill (grad2);
        g.fillRect (0.0f, 0.0f, W, H);
    }
    // grain très fin
    juce::Random rnd (21);
    for (int i = 0; i < 90; ++i)
    {
        const float x = rnd.nextFloat() * W, y = rnd.nextFloat() * H, s = (0.5f + 1.2f * rnd.nextFloat() * rnd.nextFloat()) * u;
        g.setColour (ui::mist.withAlpha (0.03f + 0.07f * rnd.nextFloat()));
        g.fillEllipse (x, y, s, s);
    }

    // en-tête : VOCAL MINI, la pastille PRO SERIES, ONIRIQUE, et un filet
    {
        const float x = logoRect.getRight() + 12.0f * u;
        const auto f1 = ui::font (ui::Face::display, 24.0f * u, true, 0.04f);
        g.setFont (f1);
        g.setColour (ui::mist);
        g.drawText ("VOCAL", juce::Rectangle<float> (x, 13.0f * u, 160.0f * u, 28.0f * u), juce::Justification::centredLeft, false);
        const float w1 = ui::textWidth (f1, "VOCAL ");
        g.setColour (ui::ember);
        g.drawText ("MINI", juce::Rectangle<float> (x + w1, 13.0f * u, 120.0f * u, 28.0f * u), juce::Justification::centredLeft, false);
        const float w2 = ui::textWidth (f1, "MINI");
        // PRO SERIES : petite pastille à côté du nom
        const auto pf = ui::font (ui::Face::mono, 8.5f * u, true, 0.26f);
        const float pw = ui::textWidth (pf, "PRO SERIES") + 14.0f * u;
        const auto pill = juce::Rectangle<float> (x + w1 + w2 + 10.0f * u, 18.0f * u, pw, 17.0f * u);
        g.setColour (ui::ember.withAlpha (0.14f));
        g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
        g.setColour (ui::ember.withAlpha (0.75f));
        g.drawRoundedRectangle (pill.reduced (0.5f), pill.getHeight() * 0.5f, std::max (1.0f, 0.9f * u));
        g.setColour (ui::lilac);
        g.setFont (pf);
        g.drawText ("PRO SERIES", pill.translated (1.5f * u, 0.0f), juce::Justification::centred, false);
        g.setColour (ui::haze);
        g.setFont (ui::font (ui::Face::mono, 9.0f * u, false, 0.34f));
        g.drawText ("ONIRIQUE", juce::Rectangle<float> (x + 1.0f * u, 42.0f * u, 160.0f * u, 12.0f * u), juce::Justification::centredLeft, false);
        juce::ColourGradient line (ui::ember.withAlpha (0.0f), 0.0f, 0.0f, ui::ember.withAlpha (0.0f), W, 0.0f, false);
        line.addColour (0.30, ui::ember.withAlpha (0.40f));
        line.addColour (0.70, ui::line.withAlpha (0.20f));
        g.setGradientFill (line);
        g.fillRect (0.0f, 64.0f * u, W, std::max (1.0f, 1.0f * u));
    }

    // colonne de gauche : un verre très léger derrière le COMP
    {
        const auto col = juce::Rectangle<float> (14.0f * u, 70.0f * u, 392.0f * u, 552.0f * u);
        g.setColour (juce::Colours::white.withAlpha (0.012f));
        g.fillRoundedRectangle (col, 16.0f * u);
        g.setColour (ui::line.withMultipliedAlpha (0.7f));
        g.drawRoundedRectangle (col.reduced (0.5f), 16.0f * u, std::max (1.0f, u));
    }

    // rangées : verre sombre, filet, titre
    for (int r = 0; r < 5; ++r)
    {
        const auto row = rows[(size_t) r];
        const float rad = 12.0f * u;
        g.setColour (ui::deep.withAlpha (0.92f));
        g.fillRoundedRectangle (row, rad);
        juce::ColourGradient sheen (ui::lilac.withAlpha (0.035f), row.getX(), row.getY(), ui::lilac.withAlpha (0.0f), row.getX(), row.getY() + row.getHeight() * 0.7f, false);
        g.setGradientFill (sheen);
        g.fillRoundedRectangle (row, rad);
        g.setColour (ui::line);
        g.drawRoundedRectangle (row.reduced (0.5f), rad, std::max (1.0f, u));

        // titre à gauche, un trait rouge vertical devant lui (fondu en haut et en bas)
        const auto titleR = juce::Rectangle<float> (row.getX() + 20.0f * u, row.getY(), (kTitleW - 26.0f) * u, row.getHeight());
        {
            const auto bar = juce::Rectangle<float> (row.getX() + 10.0f * u, row.getY() + 22.0f * u, 2.0f * u, row.getHeight() - 44.0f * u);
            juce::ColourGradient grad (ui::ember.withAlpha (0.0f), bar.getX(), bar.getY(), ui::ember.withAlpha (0.0f), bar.getX(), bar.getBottom(), false);
            grad.addColour (0.5, ui::ember.withAlpha (0.85f));
            g.setGradientFill (grad);
            g.fillRoundedRectangle (bar, bar.getWidth() * 0.5f);
        }
        g.setColour (ui::mist);
        g.setFont (ui::fitFont (ui::Face::display, 15.0f * u, titleR.getWidth(), kRowNames[r], true, 0.04f));
        g.drawText (kRowNames[r], titleR.withTrimmedBottom (row.getHeight() * 0.16f), juce::Justification::centredLeft, false);
        g.setColour (ui::haze.withAlpha (0.85f));
        g.setFont (ui::fitFont (ui::Face::mono, 8.5f * u, titleR.getWidth(), kRowSubs[r], false, 0.06f));
        g.drawText (kRowSubs[r], titleR.withTrimmedTop (row.getHeight() * 0.5f + 6.0f * u).withHeight (12.0f * u), juce::Justification::centredLeft, false);
        // séparateur vertical
        g.setColour (ui::line);
        g.fillRect (row.getX() + (kTitleW - 4.0f) * u, row.getY() + 14.0f * u, std::max (1.0f, u), row.getHeight() - 28.0f * u);
    }

    // CLEAN : séparateurs entre PLOSIVE, NOISE et GATE
    {
        const float y = rowTop (0), left = kRowX + kTitleW;
        for (float x : { left + 110.0f, left + 226.0f })
        {
            g.setColour (ui::line);
            g.fillRect (x * u, (y + 14.0f) * u, std::max (1.0f, u), (kRowH[0] - 28.0f) * u);
        }
    }

    // pied : filet
    g.setColour (ui::line.withAlpha (0.08f));
    g.fillRect (14.0f * u, (rowTop (4) + kRowH[4] + 5.0f) * u, W - 28.0f * u, std::max (1.0f, u));
}
