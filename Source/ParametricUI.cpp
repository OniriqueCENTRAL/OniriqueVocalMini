#include "ParametricUI.h"

using namespace onirique;
using namespace minidraw;
using PE = onirique::mini::VoiceParametric;

namespace
{
constexpr float kPanelW = 1092.0f, kDisplayW = 1068.0f;
const char* const kTypeNames[] { "BELL", "LOW SHELF", "HIGH SHELF", "LOW CUT", "HIGH CUT", "NOTCH" };
const char* const kTypeShort[] { "BELL", "LO SHELF", "HI SHELF", "LO CUT", "HI CUT", "NOTCH" };
const char* const kTypeTips[] {
    "BELL: boosts or cuts around the frequency; Q sets the width.",
    "LOW SHELF: raises or lowers everything below the frequency.",
    "HIGH SHELF: raises or lowers everything above the frequency.",
    "LOW CUT: removes what is below the frequency (rumble, boom). Choose the slope on the right; Q = resonance.",
    "HIGH CUT: removes what is above the frequency. Choose the slope on the right; Q = resonance.",
    "NOTCH: a very deep, narrow hole exactly on one frequency (a ringing resonance, a hum)." };

// boutons d'EQ (LOW, MID, HI, AIR) : identifiant, nom, fréquence de leur poignée sur l'écran
struct KnobHandle { const char* id; const char* name; float hz; };
// (entre les places de départ des bandes : une poignée ne tombe jamais sur un point)
const KnobHandle kHandles[4] { { ids::low, "LOW", 115.0f }, { ids::mid, "MID", 1600.0f }, { ids::hi, "HI", 7000.0f }, { ids::air, "AIR", 14000.0f } };

juce::String hzText (float hz)
{
    return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
}
juce::String dbShort (float db)
{
    db = std::round (db * 10.0f) / 10.0f + 0.0f;
    return (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
}
juce::String plusMinus (int v) { return juce::String::charToString ((juce::juce_wchar) 0x00B1) + juce::String (v); }

// petit dessin d'un type : la vraie courbe d'une bande typique, entre -12 et +12 dB
juce::Path typeIcon (int type, juce::Rectangle<float> r)
{
    PE::Band b;
    b.type = type;
    b.slope = 1;
    switch (type)
    {
        case PE::bell:      b.freq = 1000.0f; b.gainDb = 10.0f; b.q = 1.6f; break;
        case PE::lowShelf:  b.freq = 700.0f;  b.gainDb = 9.0f;  b.q = 0.71f; break;
        case PE::highShelf: b.freq = 1400.0f; b.gainDb = 9.0f;  b.q = 0.71f; break;
        case PE::lowCut:    b.freq = 400.0f;  b.q = 0.71f; break;
        case PE::highCut:   b.freq = 2500.0f; b.q = 0.71f; break;
        default:            b.freq = 1000.0f; b.q = 2.5f; break;
    }
    juce::Path p;
    constexpr int n = 28;
    for (int i = 0; i < n; ++i)
    {
        const double hz = 40.0 * std::pow (500.0, (double) i / (n - 1));
        const double db = 20.0 * std::log10 (std::max (1.0e-6, std::abs (PE::bandResponse (48000.0, b, hz, b.gainDb))));
        const float y = r.getCentreY() - (float) juce::jlimit (-12.0, 12.0, db) / 12.0f * r.getHeight() * 0.5f;
        const float x = r.getX() + r.getWidth() * (float) i / (float) (n - 1);
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    return p;
}

void setParam (juce::AudioProcessorValueTreeState& st, const juce::String& id, float v)
{
    if (auto* p = st.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }
}
float getParam (const juce::AudioProcessorValueTreeState& st, const juce::String& id)
{
    if (auto* v = st.getRawParameterValue (id)) return v->load();
    return 0.0f;
}
} // namespace

//==============================================================================
EqDisplay::EqDisplay (OniriqueVocalMiniAudioProcessor& p) : processor (p), st (p.apvts)
{
    window.resize ((size_t) kN);
    double sum = 0.0;
    for (int i = 0; i < kN; ++i)
    {
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (6.283185307179586 * i / kN));
        sum += window[(size_t) i];
    }
    windowGain = (float) (2.0 / sum);          // un sinus d'amplitude A lit 20 log10 A (dBFS)
    histIn.assign ((size_t) kN, 0.0f);
    histOut.assign ((size_t) kN, 0.0f);
    scratch.assign ((size_t) onirique::SpectrumTap::kSize, 0.0f);
    fftBuf.assign ((size_t) (2 * kN), 0.0f);
    specIn.fill (-140.0f);
    specOut.fill (-140.0f);
    for (auto* c : { &peqCurve, &peqMax, &knobCurve, &total, &selCurve }) c->fill (0.0f);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

EqDisplay::~EqDisplay()
{
    processor.getChain().setListenBand (-1);
}

PE::Band EqDisplay::band (int b) const
{
    PE::Band bd;
    bd.on = value (ids::peq ("On", b)) > 0.5f;
    bd.type = juce::jlimit (0, PE::numTypes - 1, juce::roundToInt (value (ids::peq ("Type", b))));
    bd.freq = value (ids::peq ("Freq", b));
    bd.gainDb = value (ids::peq ("Gain", b));
    bd.q = value (ids::peq ("Q", b));
    bd.slope = juce::roundToInt (value (ids::peq ("Slope", b)));
    bd.dyn = value (ids::peq ("Dyn", b)) > 0.5f;
    bd.thrDb = value (ids::peq ("Thresh", b));
    return bd;
}

float EqDisplay::value (const juce::String& id) const { return getParam (st, id); }

void EqDisplay::setValue (const juce::String& id, float v, bool gesture)
{
    if (auto* p = st.getParameter (id))
    {
        if (gesture) p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        if (gesture) p->endChangeGesture();
    }
}

void EqDisplay::select (int b)
{
    selected = juce::jlimit (0, kBands - 1, b);
    if (onSelect) onSelect (selected);
}

//------------------------------------------------------------------------------
juce::Rectangle<float> EqDisplay::plotArea() const noexcept
{
    const float u = (float) getWidth() / kDisplayW;
    return getLocalBounds().toFloat().withTrimmedLeft (14.0f * u).withTrimmedRight (48.0f * u).withTrimmedTop (30.0f * u)
                                     .withTrimmedBottom (22.0f * u);
}

float EqDisplay::xOf (float hz) const noexcept
{
    const auto p = plotArea();
    return p.getX() + std::log (std::max (1.0f, hz) / 20.0f) / std::log (1000.0f) * p.getWidth();
}

float EqDisplay::hzOf (float x) const noexcept
{
    const auto p = plotArea();
    return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (x - p.getX()) / p.getWidth()));
}

float EqDisplay::yOf (float db) const noexcept
{
    const auto p = plotArea();
    return p.getCentreY() - juce::jlimit (-3.0f * rangeDb, 3.0f * rangeDb, db) / rangeDb * p.getHeight() * 0.5f;
}

float EqDisplay::dbOf (float y) const noexcept
{
    const auto p = plotArea();
    return (p.getCentreY() - y) / (p.getHeight() * 0.5f) * rangeDb;
}

float EqDisplay::ySpec (float db) const noexcept
{
    const auto p = plotArea();
    return p.getBottom() - juce::jlimit (0.0f, 1.0f, (db + 100.0f) / 90.0f) * p.getHeight();
}

juce::Point<float> EqDisplay::nodePos (int b) const
{
    const auto bd = band (b);
    const float g = PE::hasGain (bd.type) ? juce::jlimit (-rangeDb, rangeDb, bd.gainDb) : 0.0f;
    return { xOf (bd.freq), yOf (g) };
}

int EqDisplay::nodeAt (juce::Point<float> p) const
{
    const float r = 12.0f * (float) getWidth() / kDisplayW;
    int best = -1;
    float bestD = r;
    // la bande choisie d'abord (deux points superposés : on attrape celle qu'on règle)
    if (nodePos (selected).getDistanceFrom (p) < r) return selected;
    for (int b = 0; b < kBands; ++b)
        if (const float d = nodePos (b).getDistanceFrom (p); d < bestD) { bestD = d; best = b; }
    return best;
}

juce::Point<float> EqDisplay::handlePos (int k) const
{
    const float hz = kHandles[k].hz;
    const int i = juce::jlimit (0, kPts - 1, juce::roundToInt (std::log (hz / 20.0f) / std::log (1000.0f) * (float) (kPts - 1)));
    const auto p = plotArea();
    return { xOf (hz), juce::jlimit (p.getY() + 6.0f, p.getBottom() - 6.0f, yOf (knobCurve[(size_t) i])) };
}

int EqDisplay::handleAt (juce::Point<float> p) const
{
    const float r = 10.0f * (float) getWidth() / kDisplayW;
    for (int k = 0; k < kKnobs; ++k)
        if (handlePos (k).getDistanceFrom (p) < r) return k;
    return -1;
}

//------------------------------------------------------------------------------
// chaque point de l'écran : moyenne gaussienne de 1/12 d'octave des bins voisins (voir SpectrumSmoother.h)
void EqDisplay::buildMap (double fs)
{
    mapFs = fs;
    smoother.build (fs, kN, kPts);
}

bool EqDisplay::analyse (onirique::SpectrumTap& tap, std::vector<float>& hist, int& writePos, std::array<float, kPts>& spec)
{
    bool moved = false;
    // tout ce qui est arrivé depuis la dernière analyse ; on garde les kN derniers échantillons
    const int got = tap.pull (scratch.data(), (int) scratch.size());
    for (int i = std::max (0, got - kN); i < got; ++i)
    {
        hist[(size_t) writePos] = scratch[(size_t) i];
        writePos = (writePos + 1) & (kN - 1);
    }
    for (int i = 0; i < kN; ++i)
        fftBuf[(size_t) i] = hist[(size_t) ((writePos + i) & (kN - 1))] * window[(size_t) i];
    std::fill (fftBuf.begin() + kN, fftBuf.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (fftBuf.data(), true);
    for (int k = 0; k <= kN / 2; ++k)
    {
        const float a = fftBuf[(size_t) k] * windowGain;
        fftBuf[(size_t) k] = a * a;                // puissance
    }
    std::array<float, kPts> level;
    smoother.apply (fftBuf.data(), level);
    for (int i = 0; i < kPts; ++i)
    {
        const float db = level[(size_t) i];
        auto& s = spec[(size_t) i];
        const float before = s;
        s += (db > s ? 0.70f : 0.13f) * (db - s);  // montée vive, retombée douce (~250 ms), à 30 analyses par seconde
        moved |= std::abs (s - before) > 0.05f && std::max (s, before) > -105.0f;
    }
    return moved;
}

// spectre immobile d'une voix (captures d'essai) : la sortie suit la courbe totale des EQ
void EqDisplay::demoSpectrum()
{
    for (int i = 0; i < kPts; ++i)
    {
        const float f = hzAt (i);
        const float l2 = std::log2 (f / 250.0f);
        float db = -34.0f - (f > 250.0f ? 7.5f * l2 : 0.0f) - (f < 110.0f ? 14.0f * std::log2 (110.0f / f) : 0.0f);
        for (auto [fc, amt] : { std::pair { 600.0f, 4.0f }, { 1250.0f, 3.0f }, { 2800.0f, 5.0f }, { 6500.0f, 2.5f } })
            db += amt * std::exp (-std::pow (std::log2 (f / fc) * 2.2f, 2.0f));
        db += 1.6f * std::sin ((float) i * 0.9f) * (f > 3000.0f ? 1.0f : 0.4f);
        specIn[(size_t) i] = db;
        specOut[(size_t) i] = db + total[(size_t) i];
    }
}

void EqDisplay::tick()
{
    if (! isShowing()) return;
    const double fs = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    if (std::abs (fs - mapFs) > 0.5) buildMap (fs);

    flushDrag();
    bool specChanged = false;
    if (! frozen && (++frame & 1) == 0)
    {
        auto& chain = processor.getChain();
        specChanged |= analyse (chain.getEqInputTap(), histIn, writeIn, specIn);
        specChanged |= analyse (chain.getEqOutputTap(), histOut, writeOut, specOut);
    }
    bool eqChanged = updateCurves();
    if (frozen && eqChanged) demoSpectrum();
    for (int b = 0; b < kBands; ++b)
        eqChanged |= ui::approach (nodeGlow[(size_t) b], (b == selected ? 0.6f : 0.0f) + (b == hoverNode || b == dragNode ? 0.4f : 0.0f), 0.2f);
    for (int k = 0; k < kKnobs; ++k)
        eqChanged |= ui::approach (handleGlow[(size_t) k], k == hoverHandle || k == dragHandle ? 1.0f : 0.0f, 0.2f);
    if (qFlash > 0) { --qFlash; eqChanged = true; }
    if (eqChanged || specChanged) repaint();
}

// courbes : valeurs animées vers les réglages, puis réponse exacte des filtres (recalculée seulement si quelque chose bouge)
bool EqDisplay::updateCurves()
{
    using namespace onirique::mini;
    const double rate = (processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0) * (processor.isLive() ? 1.0 : 2.0);
    bool moved = false;
    for (int k = 0; k < kKnobs; ++k) moved |= ui::approach (knobShown[(size_t) k], value (kHandles[k].id) * 0.1f, 0.3f);
    const bool byp = value (ids::bypass) > 0.5f;
    moved |= ui::approach (lowCutAmt, value (ids::lowCut) > 0.5f && ! byp ? 1.0f : 0.0f, 0.25f);
    moved |= ui::approach (eqAmt, value (ids::peqPower) > 0.5f && ! byp ? 1.0f : 0.0f, 0.2f);
    const float knobAmt = byp ? 0.0f : 1.0f;

    std::array<PE::Band, kBands> bd;
    anyDyn = false;
    for (int b = 0; b < kBands; ++b)
    {
        bd[(size_t) b] = band (b);
        const auto& x = bd[(size_t) b];
        const bool dyn = x.on && x.dyn && PE::canBeDynamic (x.type);
        anyDyn |= dyn;
        const float target = dyn && ! frozen ? processor.getChain().getPeqBandGainDb (b) : x.gainDb;
        moved |= ui::approach (liveGain[(size_t) b], target, dyn ? 0.35f : 1.0f);
    }

    std::vector<float> sig;
    sig.reserve (kBands * 8 + 12);
    for (int b = 0; b < kBands; ++b)
    {
        const auto& x = bd[(size_t) b];
        for (float v : { x.on ? 1.0f : 0.0f, (float) x.type, x.freq, x.gainDb, x.q, (float) x.slope, x.dyn ? 1.0f : 0.0f, liveGain[(size_t) b] })
            sig.push_back (v);
    }
    for (float v : knobShown) sig.push_back (v);
    for (float v : { lowCutAmt, eqAmt, knobAmt, (float) rate, (float) selected }) sig.push_back (v);
    if (! moved && sig == lastSig) return false;
    lastSig = sig;

    // coefficients une fois par bande (gain en cours et, pour les bandes dynamiques, gain réglé)
    BiquadCoeffs live[kBands][PE::kMaxStages], maxC[kBands][PE::kMaxStages];
    int ns[kBands] {};
    for (int b = 0; b < kBands; ++b)
    {
        const auto& x = bd[(size_t) b];
        if (! x.on) continue;
        ns[b] = PE::numStages (x.type, x.slope);
        for (int k = 0; k < ns[b]; ++k)
        {
            live[b][k] = PE::designStage (rate, x.type, x.freq, liveGain[(size_t) b], x.q, x.slope, k);
            maxC[b][k] = PE::designStage (rate, x.type, x.freq, x.gainDb, x.q, x.slope, k);
        }
    }
    // les boutons : LOW CUT, LOW, MID (avant le COMP), HI, AIR (après) : les mêmes filtres que le son
    BiquadCoeffs bc[BodyEQ::kF], cc[ShineEQ::kCut], sc[ShineEQ::kBoost];
    bool bon[BodyEQ::kF], con[ShineEQ::kCut], son[ShineEQ::kBoost];
    BodyEQ::design (rate, knobShown[0], knobShown[1], bc, bon);
    ShineEQ::design (rate, knobShown[2], knobShown[3], cc, con, sc, son);
    const auto lcA = BiquadCoeffs::highpass (rate, 90.0, 1.0), lcB = BiquadCoeffs::highpass1 (rate, 90.0);

    auto db = [] (std::complex<double> z) { return (float) (20.0 * std::log10 (std::max (1.0e-9, std::abs (z)))); };
    for (int i = 0; i < kPts; ++i)
    {
        const double f = std::min ((double) hzAt (i), 0.49 * rate);
        std::complex<double> hl (1.0, 0.0), hm (1.0, 0.0), hs (1.0, 0.0);
        for (int b = 0; b < kBands; ++b)
            for (int k = 0; k < ns[b]; ++k)
            {
                const auto rl = live[b][k].response (rate, f);
                hl *= rl;
                hm *= anyDyn ? maxC[b][k].response (rate, f) : rl;
                if (b == selected) hs *= maxC[b][k].response (rate, f);
            }
        const float lc = db (lcA.response (rate, f) * lcB.response (rate, f));
        const float kn = knobAmt * (cascadeDb (bc, bon, rate, f) + cascadeDb (cc, con, rate, f) + cascadeDb (sc, son, rate, f) + lowCutAmt * lc);
        peqCurve[(size_t) i] = eqAmt * db (hl);
        peqMax[(size_t) i] = eqAmt * db (hm);
        selCurve[(size_t) i] = bd[(size_t) selected].on ? db (hs) : 0.0f;
        knobCurve[(size_t) i] = kn;
        total[(size_t) i] = kn + peqCurve[(size_t) i];
    }
    return true;
}

//------------------------------------------------------------------------------
juce::String EqDisplay::getTooltip()
{
    if (hoverNode >= 0)
        return "Band " + juce::String (hoverNode + 1) + ": drag = frequency / gain (Shift = fine), mouse wheel = Q, double-click = on / off, "
               "right-click = type, slope and dynamic mode. With LISTEN on, hold the dot to hear only its band.";
    if (hoverHandle >= 0)
        return juce::String (kHandles[hoverHandle].name) + " knob of the EQ row: drag up / down to turn it from here, double-click = back to its default.";
    return "Parametric EQ: double-click an empty spot to add a band there, drag the numbered dots, right-click a dot for its menu. "
           "Red = parametric EQ, dotted pink = the LOW / MID / HI / AIR knobs and LOW CUT, white = everything together. "
           "Grey = voice in, light = voice out.";
}

void EqDisplay::mouseMove (const juce::MouseEvent& e)
{
    hoverNode = nodeAt (e.position);
    hoverHandle = hoverNode < 0 ? handleAt (e.position) : -1;
    setMouseCursor (hoverNode >= 0 ? juce::MouseCursor::DraggingHandCursor
                                   : (hoverHandle >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::CrosshairCursor));
}

void EqDisplay::mouseExit (const juce::MouseEvent&)
{
    if (dragNode < 0) hoverNode = -1;
    if (dragHandle < 0) hoverHandle = -1;
}

void EqDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (const int n = nodeAt (e.position); n >= 0)
    {
        select (n);
        if (e.mods.isPopupMenu()) { showBandMenu (*this, st, n, e.getScreenPosition()); return; }
        dragNode = n;
        dragStart = e.position;
        const auto bd = band (n);
        dragF0 = bd.freq;
        dragG0 = bd.gainDb;
        for (auto* what : { "Freq", "Gain" })
            if (auto* p = st.getParameter (ids::peq (what, n))) p->beginChangeGesture();
        if (listenArmed) processor.getChain().setListenBand (n);   // LISTEN : on entend cette bande tant qu'on la tient
        return;
    }
    if (const int h = handleAt (e.position); h >= 0 && ! e.mods.isPopupMenu())
    {
        dragHandle = h;
        dragStart = e.position;
        dragK0 = value (kHandles[h].id);
        if (auto* p = st.getParameter (kHandles[h].id)) p->beginChangeGesture();
    }
}

void EqDisplay::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = plotArea();
    const float k = e.mods.isShiftDown() ? 0.25f : 1.0f;   // Maj : réglage fin
    if (dragNode >= 0)
    {
        if (! band (dragNode).on) setValue (ids::peq ("On", dragNode), 1.0f);   // tirer une bande coupée l'allume
        const float oct = (e.position.x - dragStart.x) * k / p.getWidth() * std::log2 (1000.0f);
        pendingF = juce::jlimit (20.0f, 20000.0f, dragF0 * std::pow (2.0f, oct));
        const float dg = -(e.position.y - dragStart.y) * k / (p.getHeight() * 0.5f) * rangeDb;
        pendingG = juce::jlimit (-18.0f, 18.0f, std::round ((dragG0 + dg) * 10.0f) / 10.0f);
        pending = true;
    }
    else if (dragHandle >= 0)
    {
        const float dv = -(e.position.y - dragStart.y) * k / (p.getHeight() * 0.5f) * 10.0f;
        pendingK = juce::jlimit (-10.0f, 10.0f, std::round ((dragK0 + dv) * 10.0f) / 10.0f);
        pending = true;
    }
}

// la position voulue est retenue à chaque mouvement et envoyée au DAW une fois par image (une souris rapide envoie des
// centaines de mouvements par seconde : autant de notifications d'automation inutiles)
void EqDisplay::flushDrag()
{
    if (! pending) return;
    pending = false;
    if (dragNode >= 0)
    {
        setValue (ids::peq ("Freq", dragNode), pendingF, false);
        if (PE::hasGain (band (dragNode).type)) setValue (ids::peq ("Gain", dragNode), pendingG, false);
    }
    else if (dragHandle >= 0)
        setValue (kHandles[dragHandle].id, pendingK, false);
}

void EqDisplay::mouseUp (const juce::MouseEvent&)
{
    flushDrag();   // la dernière position, avant de clore le geste
    if (dragNode >= 0)
    {
        for (auto* what : { "Freq", "Gain" })
            if (auto* p = st.getParameter (ids::peq (what, dragNode))) p->endChangeGesture();
        dragNode = -1;
        processor.getChain().setListenBand (-1);
    }
    if (dragHandle >= 0)
    {
        if (auto* p = st.getParameter (kHandles[dragHandle].id)) p->endChangeGesture();
        dragHandle = -1;
    }
}

void EqDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const int n = nodeAt (e.position); n >= 0)
    {
        setValue (ids::peq ("On", n), band (n).on ? 0.0f : 1.0f);   // double-clic sur un point : allume / coupe la bande
        select (n);
        return;
    }
    if (const int h = handleAt (e.position); h >= 0)
    {
        if (auto* p = st.getParameter (kHandles[h].id))
        { p->beginChangeGesture(); p->setValueNotifyingHost (p->getDefaultValue()); p->endChangeGesture(); }
        return;
    }
    // double-clic dans le vide : la bande libre la plus proche en fréquence devient une cloche à cet endroit
    const float hz = juce::jlimit (20.0f, 20000.0f, hzOf (e.position.x));
    int best = -1;
    float bestD = 1.0e9f;
    for (int b = 0; b < kBands; ++b)
        if (const auto bd = band (b); ! bd.on)
            if (const float d = std::abs (std::log2 (hz / std::max (20.0f, bd.freq))); d < bestD) { bestD = d; best = b; }
    if (best < 0) return;
    setValue (ids::peq ("Type", best), (float) PE::bell);
    setValue (ids::peq ("Freq", best), hz);
    setValue (ids::peq ("Gain", best), juce::jlimit (-18.0f, 18.0f, std::round (dbOf (e.position.y) * 10.0f) / 10.0f));
    setValue (ids::peq ("Q", best), 1.0f);
    setValue (ids::peq ("Dyn", best), 0.0f);
    setValue (ids::peq ("On", best), 1.0f);
    select (best);
}

void EqDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float delta = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
    if (delta == 0.0f) return;
    // bande visée : le point sous la souris, sinon la bande allumée la plus proche en fréquence (3/4 d'octave), sinon
    // la bande choisie
    int n = nodeAt (e.position);
    if (n < 0)
    {
        const float hz = hzOf (e.position.x);
        float best = 0.75f;
        for (int b = 0; b < kBands; ++b)
        {
            const auto bd = band (b);
            const float d = std::abs (std::log2 (hz / std::max (20.0f, bd.freq)));
            if (bd.on && d < best) { best = d; n = b; }
        }
    }
    if (n < 0) n = selected;
    // pas proportionnel au défilement (molette crantée ou trackpad) : environ x1.3 par cran, vers le haut = plus étroit
    const float q = band (n).q * std::pow (2.0f, delta * (e.mods.isShiftDown() ? 0.4f : 1.6f));
    setValue (ids::peq ("Q", n), juce::jlimit (0.1f, 10.0f, q));
    select (n);
    qFlash = 80;   // l'étiquette reste ~1.3 s après le dernier cran
}

void EqDisplay::showBandMenu (juce::Component& target, juce::AudioProcessorValueTreeState& st, int b, juce::Point<int> screenPos)
{
    const int cur = juce::roundToInt (getParam (st, ids::peq ("Type", b)));
    const int slope = juce::roundToInt (getParam (st, ids::peq ("Slope", b)));
    const bool on = getParam (st, ids::peq ("On", b)) > 0.5f;
    const bool dyn = getParam (st, ids::peq ("Dyn", b)) > 0.5f;
    juce::PopupMenu menu;
    menu.setLookAndFeel (&target.getLookAndFeel());
    menu.addSectionHeader ("BAND " + juce::String (b + 1));
    for (int t = 0; t < PE::numTypes; ++t) menu.addItem (t + 1, kTypeNames[t], true, t == cur);
    {
        juce::PopupMenu sm;
        for (int s = 0; s < 4; ++s) sm.addItem (50 + s, prm::peqSlopeNames()[s], true, s == slope);
        menu.addSubMenu ("Slope", sm, PE::isCut (cur));
    }
    menu.addSeparator();
    menu.addItem (102, PE::canBeDynamic (cur) ? "Dynamic (acts only above its threshold)" : "Dynamic (bell and shelves only)",
                  PE::canBeDynamic (cur), dyn && PE::canBeDynamic (cur));
    menu.addSeparator();
    menu.addItem (100, on ? "Switch band off" : "Switch band on");
    menu.addItem (101, "Reset band");

    auto* ap = &st;
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (screenPos.x, screenPos.y, 1, 1))
                            .withParentComponent (target.findParentComponentOfClass<juce::AudioProcessorEditor>()),
                        [ap, b] (int r)
                        {
                            auto& s = *ap;
                            if (r >= 1 && r <= PE::numTypes) { setParam (s, ids::peq ("Type", b), (float) (r - 1)); setParam (s, ids::peq ("On", b), 1.0f); }
                            else if (r >= 50 && r < 54) setParam (s, ids::peq ("Slope", b), (float) (r - 50));
                            else if (r == 102)
                            {
                                const bool nowDyn = getParam (s, ids::peq ("Dyn", b)) < 0.5f;
                                setParam (s, ids::peq ("Dyn", b), nowDyn ? 1.0f : 0.0f);
                                // passage en dynamique d'une bande sans gain : une coupe de départ utile
                                if (nowDyn && std::abs (getParam (s, ids::peq ("Gain", b))) < 0.5f) setParam (s, ids::peq ("Gain", b), -4.0f);
                                setParam (s, ids::peq ("On", b), 1.0f);
                            }
                            else if (r == 100) setParam (s, ids::peq ("On", b), getParam (s, ids::peq ("On", b)) > 0.5f ? 0.0f : 1.0f);
                            else if (r == 101)
                                for (auto* what : { "Type", "Freq", "Gain", "Q", "Slope", "Dyn", "Thresh", "On" })
                                    if (auto* p = s.getParameter (ids::peq (what, b)))
                                    { p->beginChangeGesture(); p->setValueNotifyingHost (p->getDefaultValue()); p->endChangeGesture(); }
                        });
}

//------------------------------------------------------------------------------
// courbe lisse (Catmull-Rom) à travers les points de l'écran ; step > 1 : un point sur step (halos)
juce::Path EqDisplay::smoothPath (const std::array<float, kPts>& ys, int step) const
{
    std::array<juce::Point<float>, kPts + 1> pt;
    int n = 0;
    for (int i = 0; i < kPts; i += step) pt[(size_t) n++] = { xOf (hzAt (i)), ys[(size_t) i] };
    if (pt[(size_t) n - 1].x < xOf (hzAt (kPts - 1))) pt[(size_t) n++] = { xOf (hzAt (kPts - 1)), ys[(size_t) kPts - 1] };
    juce::Path p;
    p.preallocateSpace (n * 7 + 8);
    p.startNewSubPath (pt[0]);
    for (int i = 0; i < n - 1; ++i)
    {
        const auto& p0 = pt[(size_t) std::max (0, i - 1)];
        const auto& p1 = pt[(size_t) i];
        const auto& p2 = pt[(size_t) i + 1];
        const auto& p3 = pt[(size_t) std::min (n - 1, i + 2)];
        p.cubicTo (p1 + (p2 - p0) / 6.0f, p2 - (p3 - p1) / 6.0f, p2);
    }
    return p;
}

// fond, grille, repères (calque refait seulement à la taille ou à l'échelle changée)
void EqDisplay::paintGrid (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const float u = area.getWidth() / kDisplayW, es = ui::editorScale (*this);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.62f), 0.0f, area.getY(),
                                             ui::night.withAlpha (0.92f), 0.0f, area.getBottom(), false));
    g.fillRoundedRectangle (area, 12.0f * u);
    g.setColour (ui::line);
    g.drawRoundedRectangle (area.reduced (0.5f), 12.0f * u, std::max (1.0f, u));

    const auto plot = plotArea();
    const float labelH = std::max (9.0f * es, 10.0f * u);
    g.setFont (ui::font (ui::Face::mono, labelH));
    // fréquences : décades nettes, divisions fines, les repères utiles à la voix
    for (float dec : { 10.0f, 100.0f, 1000.0f, 10000.0f })
        for (int k = 1; k <= 9; ++k)
        {
            const float hz = dec * (float) k;
            if (hz < 20.0f || hz > 20000.0f) continue;
            const float x = xOf (hz);
            const bool major = k == 1;
            g.setColour (ui::line.withMultipliedAlpha (major ? 0.9f : 0.32f));
            g.fillRect (juce::Rectangle<float> (x, plot.getY(), 1.0f, plot.getHeight()));
            if (major || k == 2 || k == 5 || (k == 3 && dec < 10000.0f))
            {
                const juce::String txt = hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz);
                g.setColour (ui::haze.withAlpha (major ? 0.9f : 0.6f));
                g.drawText (txt, juce::Rectangle<float> (x + 3.0f * u, plot.getBottom() + 4.0f * u, 40.0f * u, 12.0f * u),
                            juce::Justification::centredLeft, false);
            }
        }
    // gains : un trait tous les 3 dB (6 dB au-delà de ±12), 0 dB appuyé
    const int stepDb = rangeDb > 12.5f ? 6 : (rangeDb > 6.5f ? 3 : 2);
    for (int dbi = -(int) rangeDb; dbi <= (int) rangeDb; dbi += stepDb)
    {
        const float y = yOf ((float) dbi);
        g.setColour (dbi == 0 ? ui::line.withMultipliedAlpha (2.0f) : ui::line.withMultipliedAlpha (0.45f));
        g.fillRect (juce::Rectangle<float> (plot.getX(), y, plot.getWidth(), 1.0f));
        g.setColour (ui::haze.withAlpha (dbi == 0 ? 0.9f : 0.6f));
        g.drawText ((dbi > 0 ? "+" : "") + juce::String (dbi), juce::Rectangle<float> (plot.getRight() + 6.0f * u, y - 6.0f * u, 40.0f * u, 12.0f * u),
                    juce::Justification::centredLeft, false);
    }
    g.setColour (ui::haze.withAlpha (0.6f));
    g.drawText ("dB", juce::Rectangle<float> (plot.getRight() + 6.0f * u, plot.getBottom() + 4.0f * u, 40.0f * u, 12.0f * u),
                juce::Justification::centredLeft, false);
}

void EqDisplay::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const float u = area.getWidth() / kDisplayW, es = ui::editorScale (*this);
    const auto plot = plotArea();

    // fond et grille : calque à la résolution de l'écran
    const float ps = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int w = juce::jmax (1, juce::roundToInt ((float) getWidth() * ps)), hh = juce::jmax (1, juce::roundToInt ((float) getHeight() * ps));
    if (! gridLayer.isValid() || gridLayer.getWidth() != w || gridLayer.getHeight() != hh)
    {
        gridLayer = juce::Image (juce::Image::ARGB, w, hh, true);
        juce::Graphics lg (gridLayer);
        lg.addTransform (juce::AffineTransform::scale (ps));
        paintGrid (lg);
    }
    g.drawImageTransformed (gridLayer, juce::AffineTransform::scale (1.0f / ps));

    auto yArr = [this] (const std::array<float, kPts>& v, bool spec)
    {
        std::array<float, kPts> y;
        for (int i = 0; i < kPts; ++i) y[(size_t) i] = spec ? ySpec (v[(size_t) i]) : yOf (v[(size_t) i]);
        return y;
    };
    auto closeTo = [&plot] (juce::Path p, float yBase)
    {
        p.lineTo (plot.getRight(), yBase);
        p.lineTo (plot.getX(), yBase);
        p.closeSubPath();
        return p;
    };

    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (plot.toNearestInt());

        // ---- spectres : la voix qui entre (voile gris), celle qui sort (dégradé clair + trait net)
        const auto yIn = yArr (specIn, true), yOut = yArr (specOut, true);
        const auto pIn = smoothPath (yIn), pOut = smoothPath (yOut);
        g.setGradientFill (juce::ColourGradient (ui::haze.withAlpha (0.15f), 0.0f, plot.getY(), ui::haze.withAlpha (0.0f), 0.0f, plot.getBottom(), false));
        g.fillPath (closeTo (pIn, plot.getBottom()));
        g.setColour (ui::haze.withAlpha (0.40f));
        g.strokePath (pIn, juce::PathStrokeType (std::max (1.0f, 1.0f * u)));
        juce::ColourGradient og (ui::mist.withAlpha (0.22f), 0.0f, plot.getY(), ui::mist.withAlpha (0.01f), 0.0f, plot.getBottom(), false);
        og.addColour (0.55, ui::mist.withAlpha (0.08f));
        g.setGradientFill (og);
        g.fillPath (closeTo (pOut, plot.getBottom()));
        g.setColour (ui::mist.withAlpha (0.55f));
        g.strokePath (pOut, juce::PathStrokeType (std::max (1.0f, 1.2f * u), juce::PathStrokeType::curved));

        const float y0 = yOf (0.0f);
        // ---- les boutons d'EQ (LOW, MID, HI, AIR, LOW CUT) : rose pâle, pointillés, voile léger
        {
            const auto yk = yArr (knobCurve, false);
            const auto pk = smoothPath (yk);
            g.setColour (ui::lilac.withAlpha (0.05f));
            g.fillPath (closeTo (pk, y0));
            juce::Path dashed;
            const float dash[] { 5.0f * u, 4.0f * u };
            juce::PathStrokeType (std::max (1.0f, 1.5f * u)).createDashedStroke (dashed, pk, dash, 2);
            g.setColour (ui::lilac.withAlpha (0.75f));
            g.fillPath (dashed);
        }

        // ---- bande choisie : sa zone
        if (band (selected).on)
        {
            g.setColour (ui::bone.withAlpha (0.09f * eqAmt));
            g.fillPath (closeTo (smoothPath (yArr (selCurve, false)), y0));
        }

        // ---- EQ paramétrique : voile jusqu'à 0 dB, trait rouge lumineux ; bandes dynamiques : pointillés au gain réglé
        const auto yp = yArr (peqCurve, false);
        const auto pp = smoothPath (yp);
        if (anyDyn)
        {
            juce::Path dashed;
            const float dash[] { 3.0f * u, 3.0f * u };
            juce::PathStrokeType (std::max (1.0f, 1.2f * u)).createDashedStroke (dashed, smoothPath (yArr (peqMax, false)), dash, 2);
            g.setColour (ui::ember.withAlpha (0.55f * eqAmt));
            g.fillPath (dashed);
        }
        // voile : dense près de 0 dB, il s'efface en s'éloignant (une coupe ne devient pas un bloc rouge)
        {
            const auto fillP = closeTo (pp, y0);
            const float a = 0.16f * (0.3f + 0.7f * eqAmt);
            for (int side = 0; side < 2; ++side)
            {
                juce::Graphics::ScopedSaveState s2 (g);
                g.reduceClipRegion ((side == 0 ? plot.withBottom (y0) : plot.withTop (y0)).toNearestInt());
                const float yFar = side == 0 ? plot.getY() : plot.getBottom();
                g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (a), 0.0f, y0, ui::ember.withAlpha (0.015f), 0.0f, yFar, false));
                g.fillPath (fillP);
            }
        }
        ui::strokeGlow (g, smoothPath (yp, 3), ui::ember, 2.4f * u, 0.25f + 0.65f * eqAmt);
        g.setColour (ui::ember.withAlpha (0.40f + 0.60f * eqAmt));
        g.strokePath (pp, juce::PathStrokeType (std::max (1.5f, 2.3f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // ---- TOTAL : ce que la voix reçoit de tous les EQ
        g.setColour (ui::bone.withAlpha (0.92f));
        g.strokePath (smoothPath (yArr (total, false)), juce::PathStrokeType (std::max (1.0f, 1.3f * u), juce::PathStrokeType::curved));
    }

    // ---- poignées des boutons d'EQ : un losange sur leur courbe, le nom au-dessus, la valeur pendant le réglage
    for (int k = 0; k < kKnobs; ++k)
    {
        const auto c = handlePos (k);
        const float gl = handleGlow[(size_t) k], s = (4.6f + 1.4f * gl) * u;
        juce::Path d;
        d.startNewSubPath (c.x, c.y - s); d.lineTo (c.x + s, c.y); d.lineTo (c.x, c.y + s); d.lineTo (c.x - s, c.y); d.closeSubPath();
        g.setColour (ui::night.withAlpha (0.85f));
        g.fillPath (d);
        g.setColour (ui::lilac.withAlpha (0.65f + 0.35f * gl));
        g.strokePath (d, juce::PathStrokeType (std::max (1.0f, 1.4f * u)));
        const float v = value (kHandles[k].id);
        const juce::String txt = gl > 0.5f ? juce::String (kHandles[k].name) + " " + (v > 0.0f ? "+" : "") + juce::String (v, 1)
                                           : juce::String (kHandles[k].name);
        const auto f = ui::font (ui::Face::mono, std::max (8.5f * es, 9.0f * u), true, 0.08f);
        g.setFont (f);
        g.setColour (ui::lilac.withAlpha (0.55f + 0.45f * gl));
        const float tw = ui::textWidth (f, txt) + 6.0f * u;
        const float ty = c.y - s - 15.0f * u < plot.getY() ? c.y + s + 3.0f * u : c.y - s - 15.0f * u;
        g.drawText (txt, juce::Rectangle<float> (c.x - tw * 0.5f, ty, tw, 12.0f * u), juce::Justification::centred, false);
    }

    // ---- points numérotés ; bande dynamique : rail jusqu'au gain réglé + point qui suit le gain en direct
    for (int b = 0; b < kBands; ++b)
    {
        const auto bd = band (b);
        const auto c = nodePos (b);
        const float gl = nodeGlow[(size_t) b];
        const float r = (7.5f + 2.0f * gl) * u;
        const bool dyn = bd.on && bd.dyn && PE::canBeDynamic (bd.type);
        if (dyn)
        {
            const float y0 = yOf (0.0f);
            g.setColour (ui::ember.withAlpha (0.35f));
            g.fillRoundedRectangle (juce::Rectangle<float> (c.x - 1.5f * u, std::min (y0, c.y), 3.0f * u, std::abs (c.y - y0)), 1.5f * u);
            g.setColour (ui::bone);
            g.fillEllipse (juce::Rectangle<float> (5.0f * u, 5.0f * u).withCentre ({ c.x, yOf (juce::jlimit (-rangeDb, rangeDb, liveGain[(size_t) b])) }));
        }
        const auto dot = juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c);
        juce::Path ring;
        ring.addEllipse (dot);
        if (bd.on)
        {
            ui::strokeGlow (g, ring, ui::ember, 1.4f * u, (0.4f + 0.6f * gl) * (0.4f + 0.6f * eqAmt));
            g.setColour (b == selected ? ui::ember : ui::night);
            g.fillPath (ring);
            g.setColour (b == selected ? ui::bone : ui::ember);
            g.strokePath (ring, juce::PathStrokeType (std::max (1.2f, 1.8f * u)));
            if (dyn)
            {
                const float dash[] { 2.0f * u, 2.0f * u };
                juce::Path halo, e;
                e.addEllipse (dot.expanded (3.5f * u));
                juce::PathStrokeType (std::max (1.0f, 1.0f * u)).createDashedStroke (halo, e, dash, 2);
                g.setColour (ui::ember.withAlpha (0.8f));
                g.fillPath (halo);
            }
            g.setColour (b == selected ? ui::night : ui::bone);
        }
        else
        {
            // bande coupée : point creux discret, prêt à être tiré ou double-cliqué
            g.setColour (ui::night.withAlpha (0.7f));
            g.fillPath (ring);
            g.setColour (ui::haze.withAlpha ((b == selected ? 0.6f : 0.3f) + 0.4f * gl));
            g.strokePath (ring, juce::PathStrokeType (std::max (1.0f, 1.2f * u)));
            g.setColour (ui::haze.withAlpha (0.5f + 0.4f * gl));
        }
        g.setFont (ui::font (ui::Face::mono, r * 1.15f, true));
        g.drawText (juce::String (b + 1), dot, juce::Justification::centred, false);
    }

    // ---- étiquette de la bande touchée (survol, réglage, molette)
    const int tagBand = dragNode >= 0 ? dragNode : (hoverNode >= 0 ? hoverNode : (qFlash > 0 ? selected : -1));
    if (tagBand >= 0)
    {
        const auto bd = band (tagBand);
        juce::String txt = juce::String (tagBand + 1) + "  " + hzText (bd.freq);
        if (PE::hasGain (bd.type)) txt << "  " << dbShort (bd.gainDb);
        txt << "  Q " << juce::String (bd.q, 2);
        const auto c = nodePos (tagBand);
        const auto f = ui::font (ui::Face::mono, std::max (9.0f * es, 10.5f * u), true);
        const float tw = ui::textWidth (f, txt) + 16.0f * u, th = 20.0f * u;
        auto box = juce::Rectangle<float> (tw, th).withCentre ({ c.x, c.y - 24.0f * u });
        if (box.getY() < area.getY() + 2.0f) box.setCentre (c.x, c.y + 24.0f * u);
        box = box.withX (juce::jlimit (area.getX() + 4.0f, area.getRight() - tw - 4.0f, box.getX()));
        g.setColour (ui::night.withAlpha (0.94f));
        g.fillRoundedRectangle (box, th * 0.5f);
        g.setColour (ui::ember);
        g.drawRoundedRectangle (box, th * 0.5f, std::max (1.0f, u));
        g.setColour (ui::bone);
        g.setFont (f);
        g.drawText (txt, box, juce::Justification::centred, false);
    }

    // ---- légende (en haut à gauche) et état (en haut à droite)
    {
        float x = plot.getX();
        const float y = area.getY() + 9.0f * u;
        const auto f = ui::font (ui::Face::mono, std::max (8.5f * es, 9.0f * u), true, 0.12f);
        g.setFont (f);
        struct Item { juce::Colour c; const char* txt; bool dashed; };
        for (const auto& it : { Item { ui::ember, "PARAMETRIC", false }, Item { ui::lilac, "LOW / MID / HI / AIR / LOW CUT", true },
                                Item { ui::bone, "TOTAL", false }, Item { ui::mist.withAlpha (0.6f), "VOICE OUT", false },
                                Item { ui::haze, "VOICE IN", false } })
        {
            g.setColour (it.c);
            if (it.dashed)
                for (int d = 0; d < 3; ++d) g.fillRect (juce::Rectangle<float> (x + (float) d * 5.0f * u, y + 5.0f * u, 3.0f * u, 2.0f * u));
            else
                g.fillRoundedRectangle (juce::Rectangle<float> (x, y + 4.5f * u, 14.0f * u, 3.0f * u), 1.5f * u);
            g.setColour (ui::haze);
            const float tw = ui::textWidth (f, it.txt) + 4.0f * u;
            g.drawText (it.txt, juce::Rectangle<float> (x + 20.0f * u, y, tw, 12.0f * u), juce::Justification::centredLeft, false);
            x += 20.0f * u + tw + 18.0f * u;
        }
        juce::String state;
        juce::Colour col = ui::ember;
        if (eqAmt < 0.5f) { state = value (ids::bypass) > 0.5f ? "BYPASS" : "PARAMETRIC EQ OFF"; col = ui::lilac; }
        else if (listenArmed) state = "LISTEN: HOLD A DOT TO HEAR ITS BAND";
        else if (anyDyn) state = "DYNAMIC: DOTTED = FULL SETTING, SOLID = RIGHT NOW";
        if (state.isNotEmpty())
        {
            g.setColour (col);
            g.drawText (state, juce::Rectangle<float> (plot.getRight() - 420.0f * u, y, 420.0f * u, 12.0f * u), juce::Justification::centredRight, false);
        }
    }
}

//==============================================================================
PeqChip::PeqChip (juce::AudioProcessorValueTreeState& s, int b) : st (s), band (b)
{
    setTooltip ("Band " + juce::String (b + 1) + ": click to select it, click its light to switch it on / off, right-click for its menu.");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void PeqChip::tick()
{
    bool changed = ui::approach (lit, getParam (st, ids::peq ("On", band)) > 0.5f ? 1.0f : 0.0f, 0.2f);
    changed |= ui::approach (glow, hover ? 1.0f : 0.0f, 0.2f);
    const int t = juce::roundToInt (getParam (st, ids::peq ("Type", band)));
    if (t != shownType) { shownType = t; changed = true; }
    if (changed) repaint();
}

void PeqChip::mouseDown (const juce::MouseEvent& e)
{
    if (onClick) onClick (band);
    if (e.mods.isPopupMenu()) { EqDisplay::showBandMenu (*this, st, band, e.getScreenPosition()); return; }
    if (e.position.x < (float) getHeight() * 0.95f)   // le témoin : allume / coupe
        setParam (st, ids::peq ("On", band), getParam (st, ids::peq ("On", band)) > 0.5f ? 0.0f : 1.0f);
}

void PeqChip::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.5f);
    const float h = r.getHeight(), u = h / 30.0f;
    g.setColour (juce::Colours::black.withAlpha (0.38f));
    g.fillRoundedRectangle (r, h * 0.5f);
    if (selected)
    {
        g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.30f), r.getX(), r.getY(), ui::blood.withAlpha (0.55f), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, h * 0.5f);
    }
    g.setColour (selected ? ui::ember.interpolatedWith (ui::lilac, 0.3f) : ui::line.interpolatedWith (ui::ember, 0.25f * lit + 0.4f * glow));
    g.drawRoundedRectangle (r.reduced (0.5f), h * 0.5f, std::max (1.0f, 1.1f * u));

    const float d = h * 0.26f;
    const auto led = juce::Rectangle<float> (d, d).withCentre ({ r.getX() + h * 0.42f, r.getCentreY() });
    g.setColour (ui::ember.withAlpha (0.35f * lit));
    g.fillEllipse (led.expanded (d * 0.7f));
    g.setColour (ui::haze.withAlpha (0.35f).interpolatedWith (ui::ember, lit));
    g.fillEllipse (led);

    auto t = r.withTrimmedLeft (h * 0.70f).withTrimmedRight (h * 0.30f);
    g.setColour (selected ? ui::bone : ui::mist.interpolatedWith (ui::bone, glow));
    g.setFont (ui::font (ui::Face::mono, h * 0.46f, true));
    g.drawText (juce::String (band + 1), t.removeFromLeft (h * 0.40f), juce::Justification::centredLeft, false);
    const auto icon = typeIcon (juce::jlimit (0, PE::numTypes - 1, shownType), t.reduced (0.0f, h * 0.28f));
    g.setColour ((selected ? ui::bone : ui::lilac).withAlpha (0.45f + 0.55f * lit));
    g.strokePath (icon, juce::PathStrokeType (std::max (1.1f, 1.5f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
int PeqTypeSelector::cellAt (float x) const noexcept
{
    return juce::jlimit (0, PE::numTypes - 1, (int) (x / ((float) getWidth() / (float) PE::numTypes)));
}

juce::String PeqTypeSelector::getTooltip() { return hover >= 0 ? kTypeTips[hover] : "Type of the selected band."; }

void PeqTypeSelector::mouseMove (const juce::MouseEvent& e) { hover = cellAt (e.position.x); setMouseCursor (juce::MouseCursor::PointingHandCursor); }

void PeqTypeSelector::mouseDown (const juce::MouseEvent& e)
{
    // choisir un type, c'est vouloir la bande : elle s'allume
    setParam (st, ids::peq ("Type", band), (float) cellAt (e.position.x));
    setParam (st, ids::peq ("On", band), 1.0f);
}

void PeqTypeSelector::tick()
{
    const int cur = juce::roundToInt (getParam (st, ids::peq ("Type", band)));
    bool changed = false;
    if (slide < 0.0f) { slide = (float) cur; changed = true; }
    changed |= ui::approach (slide, (float) cur, 0.28f);
    for (int i = 0; i < PE::numTypes; ++i) changed |= ui::approach (glow[(size_t) i], i == hover ? 1.0f : 0.0f, 0.2f);
    if (changed) repaint();
}

void PeqTypeSelector::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float h = r.getHeight(), u = h / 30.0f, cw = r.getWidth() / (float) PE::numTypes;
    g.setColour (juce::Colours::black.withAlpha (0.36f));
    g.fillRoundedRectangle (r, h * 0.5f);
    g.setColour (ui::line);
    g.drawRoundedRectangle (r.reduced (0.5f), h * 0.5f, std::max (1.0f, 1.1f * u));
    const auto pill = juce::Rectangle<float> (r.getX() + cw * slide, r.getY(), cw, h).reduced (2.5f * u);
    g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.32f), pill.getX(), pill.getY(), ui::blood.withAlpha (0.70f), pill.getX(), pill.getBottom(), false));
    g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
    g.setColour (ui::ember.interpolatedWith (ui::lilac, 0.35f));
    g.drawRoundedRectangle (pill, pill.getHeight() * 0.5f, std::max (1.0f, 1.1f * u));
    const int cur = juce::roundToInt (getParam (st, ids::peq ("Type", band)));
    for (int i = 0; i < PE::numTypes; ++i)
    {
        const auto cell = juce::Rectangle<float> (r.getX() + cw * (float) i, r.getY(), cw, h).reduced (cw * 0.24f, h * 0.30f);
        g.setColour (i == cur ? ui::bone : ui::haze.interpolatedWith (ui::bone, 0.25f + 0.6f * glow[(size_t) i]));
        g.strokePath (typeIcon (i, cell), juce::PathStrokeType (std::max (1.2f, 1.6f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

//==============================================================================
juce::String PeqSlopeSelector::getTooltip()
{
    return "Slope of a LOW CUT / HIGH CUT band: 12 dB/oct is gentle and natural, 48 dB/oct is a wall.";
}

void PeqSlopeSelector::mouseMove (const juce::MouseEvent& e)
{
    hover = juce::jlimit (0, 3, (int) (e.position.x / ((float) getWidth() / 4.0f)));
    setMouseCursor (enabled > 0.5f ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void PeqSlopeSelector::mouseDown (const juce::MouseEvent& e)
{
    if (enabled < 0.5f) return;
    setParam (st, ids::peq ("Slope", band), (float) juce::jlimit (0, 3, (int) (e.position.x / ((float) getWidth() / 4.0f))));
}

void PeqSlopeSelector::tick()
{
    const int cur = juce::roundToInt (getParam (st, ids::peq ("Slope", band)));
    const bool cut = PE::isCut (juce::roundToInt (getParam (st, ids::peq ("Type", band))));
    bool changed = false;
    if (slide < 0.0f) { slide = (float) cur; changed = true; }
    changed |= ui::approach (slide, (float) cur, 0.28f);
    changed |= ui::approach (enabled, cut ? 1.0f : 0.0f, 0.2f);
    for (int i = 0; i < 4; ++i) changed |= ui::approach (glow[(size_t) i], i == hover && cut ? 1.0f : 0.0f, 0.2f);
    if (changed) repaint();
}

void PeqSlopeSelector::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float h = r.getHeight(), u = h / 30.0f, cw = r.getWidth() / 4.0f;
    const float a = 0.35f + 0.65f * enabled;
    g.setColour (juce::Colours::black.withAlpha (0.36f));
    g.fillRoundedRectangle (r, h * 0.5f);
    g.setColour (ui::line);
    g.drawRoundedRectangle (r.reduced (0.5f), h * 0.5f, std::max (1.0f, 1.1f * u));
    if (enabled > 0.01f)
    {
        const auto pill = juce::Rectangle<float> (r.getX() + cw * slide, r.getY(), cw, h).reduced (2.5f * u);
        g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.32f * enabled), pill.getX(), pill.getY(), ui::blood.withAlpha (0.70f * enabled),
                                                 pill.getX(), pill.getBottom(), false));
        g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
        g.setColour (ui::ember.interpolatedWith (ui::lilac, 0.35f).withAlpha (enabled));
        g.drawRoundedRectangle (pill, pill.getHeight() * 0.5f, std::max (1.0f, 1.1f * u));
    }
    const int cur = juce::roundToInt (getParam (st, ids::peq ("Slope", band)));
    g.setFont (ui::font (ui::Face::mono, h * 0.40f, true, 0.04f));
    for (int i = 0; i < 4; ++i)
    {
        const auto cell = juce::Rectangle<float> (r.getX() + cw * (float) i, r.getY(), cw, h);
        g.setColour ((i == cur && enabled > 0.5f ? ui::bone : ui::haze.interpolatedWith (ui::bone, 0.2f + 0.6f * glow[(size_t) i])).withAlpha (a));
        g.drawText (juce::String (12 * (i + 1)), cell, juce::Justification::centred, false);
    }
}

//==============================================================================
void RangeSelector::mouseMove (const juce::MouseEvent& e)
{
    hover = juce::jlimit (0, 2, (int) (e.position.x / ((float) getWidth() / 3.0f)));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void RangeSelector::mouseDown (const juce::MouseEvent& e)
{
    index = juce::jlimit (0, 2, (int) (e.position.x / ((float) getWidth() / 3.0f)));
    if (onChange) onChange (index);
}

void RangeSelector::tick()
{
    bool changed = ui::approach (slide, (float) index, 0.28f);
    for (int i = 0; i < 3; ++i) changed |= ui::approach (glow[(size_t) i], i == hover ? 1.0f : 0.0f, 0.2f);
    if (changed) repaint();
}

void RangeSelector::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float h = r.getHeight(), u = h / 26.0f, cw = r.getWidth() / 3.0f;
    g.setColour (juce::Colours::black.withAlpha (0.36f));
    g.fillRoundedRectangle (r, h * 0.5f);
    g.setColour (ui::line);
    g.drawRoundedRectangle (r.reduced (0.5f), h * 0.5f, std::max (1.0f, 1.1f * u));
    const auto pill = juce::Rectangle<float> (r.getX() + cw * slide, r.getY(), cw, h).reduced (2.5f * u);
    g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.32f), pill.getX(), pill.getY(), ui::blood.withAlpha (0.70f), pill.getX(), pill.getBottom(), false));
    g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
    g.setColour (ui::ember.interpolatedWith (ui::lilac, 0.35f));
    g.drawRoundedRectangle (pill, pill.getHeight() * 0.5f, std::max (1.0f, 1.1f * u));
    g.setFont (ui::font (ui::Face::mono, h * 0.42f, true, 0.04f));
    static constexpr int values[3] { 6, 12, 18 };
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (i == index ? ui::bone : ui::haze.interpolatedWith (ui::bone, 0.2f + 0.6f * glow[(size_t) i]));
        g.drawText (plusMinus (values[i]) + " dB", juce::Rectangle<float> (r.getX() + cw * (float) i, r.getY(), cw, h), juce::Justification::centred, false);
    }
}

//==============================================================================
ParametricPanel::ParametricPanel (OniriqueVocalMiniAudioProcessor& p) : display (p), processor (p), st (p.apvts)
{
    using K = MiniKnob::Size;
    for (int b = 0; b < EqDisplay::kBands; ++b)
    {
        chips[(size_t) b] = std::make_unique<PeqChip> (st, b);
        chips[(size_t) b]->onClick = [this] (int band) { selectBand (band); };
        addAndMakeVisible (*chips[(size_t) b]);
        kids.push_back (chips[(size_t) b].get());

        auto& s = sets[(size_t) b];
        s.freq = std::make_unique<MiniKnob> (st, ids::peq ("Freq", b), "FREQ", K::small, false,
                                             "Frequency of the band (20 Hz to 20 kHz). Also: drag its dot left / right on the screen.");
        s.gain = std::make_unique<MiniKnob> (st, ids::peq ("Gain", b), "GAIN", K::small, true,
                                             "Boost or cut of the band (-18 to +18 dB). Bell and shelves only.");
        s.q = std::make_unique<MiniKnob> (st, ids::peq ("Q", b), "Q", K::small, false,
                                          "Width of the band: low = wide and musical, high = narrow and surgical. LOW / HIGH CUT: "
                                          "resonance at the cut (0.71 = perfectly flat). Also: mouse wheel over the screen.");
        s.thr = std::make_unique<MiniKnob> (st, ids::peq ("Thresh", b), "THRESH", K::small, false,
                                            "Dynamic mode: the band only acts when its zone goes over this level, and reaches its full "
                                            "gain 9 dB above. Watch the dot follow the voice on the screen.");
        s.on = std::make_unique<PillToggle> ("ON", PillToggle::Style::small);
        s.on->setTooltip ("Band on / off (it fades in and out, no click).");
        s.onAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (st, ids::peq ("On", b), *s.on);
        s.dyn = std::make_unique<PillToggle> ("DYNAMIC", PillToggle::Style::small);
        s.dyn->setTooltip ("Dynamic band: it only cuts (or boosts) when its zone gets loud, like a de-esser for any frequency. "
                           "Perfect for a resonance or a harshness that only shows on loud notes. Bell and shelves only.");
        s.dynAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (st, ids::peq ("Dyn", b), *s.dyn);
        for (juce::Component* c : { (juce::Component*) s.freq.get(), (juce::Component*) s.gain.get(), (juce::Component*) s.q.get(),
                                    (juce::Component*) s.thr.get(), (juce::Component*) s.on.get(), (juce::Component*) s.dyn.get() })
            addChildComponent (*c);
        for (Animated* a : { (Animated*) s.freq.get(), (Animated*) s.gain.get(), (Animated*) s.q.get(), (Animated*) s.thr.get(),
                             (Animated*) s.on.get(), (Animated*) s.dyn.get() })
            kids.push_back (a);
    }

    powerAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (st, ids::peqPower, power);
    power.setTooltip ("The whole parametric EQ on / off, to compare (the LOW / MID / HI / AIR knobs stay as they are).");
    listen.setClickingTogglesState (true);
    listen.setTooltip ("LISTEN: while on, holding a dot on the screen lets you hear only the zone of that band "
                       "(to find a resonance or a harshness). Release to hear the whole voice again.");
    listen.onClick = [this] { display.setListenArmed (listen.getToggleState()); };
    reset.setTooltip ("Back to the starting point of the selected band (off, at its default place).");
    reset.onClick = [this] { resetBand (selected); };
    range.setIndex ((int) st.state.getProperty ("peqRange", 1));
    range.onChange = [this] (int i)
    {
        static constexpr float dbs[3] { 6.0f, 12.0f, 18.0f };
        display.setRange (dbs[juce::jlimit (0, 2, i)]);
        st.state.setProperty ("peqRange", i, nullptr);   // réglage d'affichage, gardé avec le projet
    };
    {
        static constexpr float dbs[3] { 6.0f, 12.0f, 18.0f };
        display.setRange (dbs[juce::jlimit (0, 2, (int) st.state.getProperty ("peqRange", 1))]);
    }
    display.onSelect = [this] (int b) { selectBand (b); };

    for (juce::Component* c : { (juce::Component*) &display, (juce::Component*) &typeSel, (juce::Component*) &slopeSel, (juce::Component*) &range,
                                (juce::Component*) &power, (juce::Component*) &listen, (juce::Component*) &reset, (juce::Component*) &close })
        addAndMakeVisible (*c);
    for (Animated* a : { (Animated*) &display, (Animated*) &typeSel, (Animated*) &slopeSel, (Animated*) &range, (Animated*) &power,
                         (Animated*) &listen, (Animated*) &reset, (Animated*) &close })
        kids.push_back (a);

    // bande de départ : la première allumée, sinon la 3 (une cloche dans le bas-médium)
    int first = 2;
    for (int b = EqDisplay::kBands - 1; b >= 0; --b)
        if (getParam (st, ids::peq ("On", b)) > 0.5f) first = b;
    selectBand (first);
}

ParametricPanel::~ParametricPanel()
{
    processor.getChain().setAnalyzerActive (false);
}

void ParametricPanel::visibilityChanged()
{
    processor.getChain().setAnalyzerActive (isVisible());
    if (! isVisible()) processor.getChain().setListenBand (-1);
}

void ParametricPanel::selectBand (int b)
{
    selected = juce::jlimit (0, EqDisplay::kBands - 1, b);
    if (display.getSelected() != selected) display.select (selected);
    for (int i = 0; i < EqDisplay::kBands; ++i)
    {
        auto& s = sets[(size_t) i];
        const bool show = i == selected;
        for (juce::Component* c : { (juce::Component*) s.freq.get(), (juce::Component*) s.gain.get(), (juce::Component*) s.q.get(),
                                    (juce::Component*) s.thr.get(), (juce::Component*) s.on.get(), (juce::Component*) s.dyn.get() })
            c->setVisible (show);
        chips[(size_t) i]->setSelected (show);
    }
    typeSel.setBand (selected);
    slopeSel.setBand (selected);
    repaint();
}

void ParametricPanel::resetBand (int b)
{
    for (auto* what : { "Type", "Freq", "Gain", "Q", "Slope", "Dyn", "Thresh", "On" })
        if (auto* p = st.getParameter (ids::peq (what, b)))
        { p->beginChangeGesture(); p->setValueNotifyingHost (p->getDefaultValue()); p->endChangeGesture(); }
}

void ParametricPanel::resized()
{
    const auto b = getLocalBounds().toFloat();
    const float u = b.getWidth() / kPanelW;
    auto R = [u] (float x, float y, float w, float h) { return juce::Rectangle<float> (x * u, y * u, w * u, h * u).toNearestInt(); };
    close.setBounds (R (kPanelW - 96.0f, 14.0f, 80.0f, 26.0f));
    range.setBounds (R (kPanelW - 96.0f - 12.0f - 168.0f, 14.0f, 168.0f, 26.0f));
    power.setBounds (R (kPanelW - 96.0f - 12.0f - 168.0f - 12.0f - 100.0f, 13.0f, 100.0f, 28.0f));
    display.setBounds (R (12.0f, 52.0f, kDisplayW, 332.0f));

    // rangée des bandes : les 8 pastilles, le type, la pente, la remise à zéro
    const float ya = 394.0f;
    for (int i = 0; i < EqDisplay::kBands; ++i) chips[(size_t) i]->setBounds (R (16.0f + 62.0f * (float) i, ya, 58.0f, 32.0f));
    typeSel.setBounds (R (522.0f, ya, 270.0f, 32.0f));
    slopeSel.setBounds (R (804.0f, ya, 160.0f, 32.0f));
    reset.setBounds (R (976.0f, ya + 2.0f, 102.0f, 28.0f));

    // réglages de la bande choisie
    const float yb = 434.0f;
    for (auto& s : sets)
    {
        s.on->setBounds (R (16.0f, yb + 66.0f, 84.0f, 28.0f));
        s.freq->setBounds (R (268.0f, yb + 8.0f, 84.0f, 96.0f));
        s.gain->setBounds (R (364.0f, yb + 8.0f, 84.0f, 96.0f));
        s.q->setBounds (R (460.0f, yb + 8.0f, 84.0f, 96.0f));
        s.dyn->setBounds (R (578.0f, yb + 22.0f, 112.0f, 28.0f));
        s.thr->setBounds (R (700.0f, yb + 8.0f, 84.0f, 96.0f));
    }
    listen.setBounds (R (108.0f, yb + 66.0f, 120.0f, 28.0f));
}

void ParametricPanel::tick()
{
    for (auto* k : kids) k->tick();
    // GAIN : estompé pour un type sans gain ; THRESH : estompé hors mode dynamique
    const int type = juce::roundToInt (getParam (st, ids::peq ("Type", selected)));
    const bool hasGain = PE::hasGain (type), dyn = getParam (st, ids::peq ("Dyn", selected)) > 0.5f && PE::canBeDynamic (type);
    auto& s = sets[(size_t) selected];
    if (ui::approach (gainAlpha, hasGain ? 1.0f : 0.3f, 0.2f)) s.gain->setAlpha (gainAlpha);
    if (ui::approach (thrAlpha, dyn ? 1.0f : 0.3f, 0.2f)) s.thr->setAlpha (thrAlpha);
    s.gain->setEnabled (hasGain);
    s.thr->setEnabled (dyn);
    s.dyn->setEnabled (PE::canBeDynamic (type));
    s.dyn->setAlpha (PE::canBeDynamic (type) ? 1.0f : 0.35f);
    s.gain->setAlpha (gainAlpha);
    s.thr->setAlpha (thrAlpha);

    juce::String title = "BAND " + juce::String (selected + 1) + "|" + kTypeNames[juce::jlimit (0, PE::numTypes - 1, type)];
    if (PE::isCut (type)) title << " " << 12 * (1 + juce::roundToInt (getParam (st, ids::peq ("Slope", selected)))) << " dB/OCT";
    if (dyn) title << "  DYNAMIC";
    if (title != shownTitle)
    {
        shownTitle = title;
        const float u = (float) getWidth() / kPanelW;
        repaint (juce::Rectangle<float> (0.0f, 430.0f * u, 262.0f * u, 64.0f * u).toNearestInt());
    }
}

void ParametricPanel::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float u = b.getWidth() / kPanelW;
    // panneau opaque : ce qui est dessous ne transparaît pas
    g.setColour (ui::night);
    g.fillRoundedRectangle (b, 14.0f * u);
    g.setColour (ui::deep2.withAlpha (0.55f));
    g.fillRoundedRectangle (b, 14.0f * u);
    glassPanel (g, b, 14.0f * u, u, 1.5f);

    g.setColour (ui::mist);
    g.setFont (ui::font (ui::Face::display, 17.0f * u, true, 0.06f));
    g.drawText ("PARAMETRIC EQ", juce::Rectangle<float> (18.0f * u, 10.0f * u, 300.0f * u, 24.0f * u), juce::Justification::centredLeft, false);
    g.setColour (ui::haze);
    g.setFont (ui::font (ui::Face::mono, 9.0f * u, false, 0.10f));
    g.drawText ("8 BANDS  /  ANALOG-MATCHED FILTERS  /  ZERO LATENCY  /  AFTER COMP AND MULTIBAND, BEFORE HI / AIR",
                juce::Rectangle<float> (18.0f * u, 33.0f * u, 640.0f * u, 14.0f * u), juce::Justification::centredLeft, false);

    // filets de la barre du bas
    g.setColour (ui::line);
    g.fillRect (juce::Rectangle<float> (16.0f * u, 432.0f * u, b.getWidth() - 32.0f * u, std::max (1.0f, u)));
    for (float x : { 252.0f, 560.0f, 796.0f })
        g.fillRect (juce::Rectangle<float> (x * u, 444.0f * u, std::max (1.0f, u), 96.0f * u));

    // la bande choisie : numéro et type
    const auto parts = juce::StringArray::fromTokens (shownTitle, "|", "");
    g.setColour (ui::bone);
    g.setFont (ui::font (ui::Face::display, 15.0f * u, true, 0.08f));
    g.drawText (parts[0], juce::Rectangle<float> (18.0f * u, 442.0f * u, 230.0f * u, 20.0f * u), juce::Justification::centredLeft, false);
    g.setColour (ui::lilac);
    g.setFont (ui::fitFont (ui::Face::mono, 10.0f * u, 230.0f * u, parts[1], true, 0.10f));
    g.drawText (parts[1], juce::Rectangle<float> (18.0f * u, 464.0f * u, 230.0f * u, 16.0f * u), juce::Justification::centredLeft, false);

    // mode d'emploi, à droite
    g.setColour (ui::haze);
    const auto f = ui::font (ui::Face::mono, 8.6f * u, false, 0.06f);
    g.setFont (f);
    const char* lines[] { "DRAG A DOT  :  FREQ / GAIN  (SHIFT = FINE)", "MOUSE WHEEL  :  Q", "DOUBLE-CLICK A DOT  :  ON / OFF",
                          "DOUBLE-CLICK EMPTY  :  NEW BAND THERE", "RIGHT-CLICK A DOT  :  TYPE, SLOPE, DYNAMIC",
                          "PINK DIAMONDS  :  THE EQ ROW KNOBS" };
    float y = 446.0f * u;
    for (auto* l : lines)
    {
        g.drawText (l, juce::Rectangle<float> (812.0f * u, y, 270.0f * u, 14.0f * u), juce::Justification::centredLeft, false);
        y += 15.5f * u;
    }
}

//==============================================================================
#if ONIRIQUE_UI_TEST
namespace
{
juce::MouseEvent fakeEvent (juce::Component& c, juce::Point<float> pos, juce::Point<float> down, int clicks, bool dragged,
                            juce::ModifierKeys mods = {})
{
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                             &c, &c, now, down, now, clicks, dragged);
}
} // namespace

juce::String EqDisplay::runSelfTest (int& failures)
{
    juce::String out;
    auto check = [&] (bool ok, const juce::String& what) { out << (ok ? "ok     " : "ECHEC  ") << what << "\n"; if (! ok) ++failures; };
    auto near = [] (float a, float b, float tol) { return std::abs (a - b) <= tol; };
    const auto p = plotArea();

    // 1) double-clic dans le vide à 1 kHz, +6 dB : une cloche allumée là (la bande libre la plus proche : la 4, 900 Hz)
    const juce::Point<float> at1k { xOf (1000.0f), yOf (6.0f) };
    mouseDoubleClick (fakeEvent (*this, at1k, at1k, 2, false));
    const int b = selected;
    auto bd = band (b);
    check (bd.on && bd.type == PE::bell && near (bd.freq, 1000.0f, 20.0f) && near (bd.gainDb, 6.0f, 0.15f),
           "double-clic a 1 kHz / +6 dB : bande " + juce::String (b + 1) + " allumee, cloche, " + juce::String (bd.freq, 0) + " Hz, "
           + juce::String (bd.gainDb, 1) + " dB");

    // 2) tirer le point d'une octave vers la droite et de 3 dB vers le bas
    {
        const auto c = nodePos (b);
        const float dx = p.getWidth() / std::log2 (1000.0f), dy = 3.0f / rangeDb * p.getHeight() * 0.5f;
        mouseDown (fakeEvent (*this, c, c, 1, false));
        mouseDrag (fakeEvent (*this, c + juce::Point<float> (dx * 0.5f, dy * 0.5f), c, 1, true));
        mouseDrag (fakeEvent (*this, c + juce::Point<float> (dx, dy), c, 1, true));
        mouseUp (fakeEvent (*this, c + juce::Point<float> (dx, dy), c, 1, true));
        bd = band (b);
        check (near (bd.freq, 2000.0f, 40.0f) && near (bd.gainDb, 3.0f, 0.15f),
               "point tire d'une octave a droite et de 3 dB vers le bas : " + juce::String (bd.freq, 0) + " Hz, " + juce::String (bd.gainDb, 1) + " dB");
    }
    // 3) même geste avec Maj : quatre fois plus fin
    {
        const auto c = nodePos (b);
        const float dx = p.getWidth() / std::log2 (1000.0f);
        mouseDown (fakeEvent (*this, c, c, 1, false, juce::ModifierKeys::shiftModifier));
        mouseDrag (fakeEvent (*this, c + juce::Point<float> (dx, 0.0f), c, 1, true, juce::ModifierKeys::shiftModifier));
        mouseUp (fakeEvent (*this, c + juce::Point<float> (dx, 0.0f), c, 1, true, juce::ModifierKeys::shiftModifier));
        bd = band (b);
        check (near (bd.freq, 2000.0f * std::pow (2.0f, 0.25f), 45.0f), "Maj : un quart d'octave pour la meme distance : " + juce::String (bd.freq, 0) + " Hz");
    }
    // 4) molette sur le point : Q plus étroit
    {
        const float q0 = band (b).q;
        juce::MouseWheelDetails w {};
        w.deltaY = 0.25f;
        const auto c = nodePos (b);
        mouseWheelMove (fakeEvent (*this, c, c, 0, false), w);
        const float q1 = band (b).q;
        check (near (q1, q0 * std::pow (2.0f, 0.4f), 0.03f), "molette : Q " + juce::String (q0, 2) + " -> " + juce::String (q1, 2));
    }
    // 5) LISTEN : tenir le point fait écouter sa bande, le relâcher rend toute la voix
    {
        setListenArmed (true);
        const auto c = nodePos (b);
        mouseDown (fakeEvent (*this, c, c, 1, false));
        const int during = processor.getChain().getListenBand();
        mouseUp (fakeEvent (*this, c, c, 1, false));
        const int after = processor.getChain().getListenBand();
        setListenArmed (false);
        check (during == b && after == -1, "LISTEN : bande " + juce::String (during + 1) + " pendant qu'on tient le point, aucune apres");
    }
    // 6) double-clic sur le point : la bande se coupe
    {
        const auto c = nodePos (b);
        mouseDoubleClick (fakeEvent (*this, c, c, 2, false));
        check (! band (b).on, "double-clic sur le point : bande coupee");
    }
    // 7) poignée LOW : tirée d'un quart de la hauteur vers le haut = +5 ; double-clic = valeur par défaut
    {
        updateCurves();
        const auto c = handlePos (0);
        const float dy = -p.getHeight() * 0.25f;
        mouseDown (fakeEvent (*this, c, c, 1, false));
        mouseDrag (fakeEvent (*this, c + juce::Point<float> (0.0f, dy), c, 1, true));
        mouseUp (fakeEvent (*this, c + juce::Point<float> (0.0f, dy), c, 1, true));
        const float v = value (ids::low);
        check (near (v, 5.0f, 0.15f), "poignee LOW tiree vers le haut : bouton LOW a " + juce::String (v, 1));
        for (int i = 0; i < 40; ++i) updateCurves();   // la courbe des boutons rejoint le réglage
        const auto c2 = handlePos (0);
        mouseDoubleClick (fakeEvent (*this, c2, c2, 2, false));
        check (near (value (ids::low), 0.0f, 0.01f), "double-clic sur la poignee : LOW revient a 0");
    }
    return out;
}

juce::String ParametricPanel::runSelfTest()
{
    int failures = 0;
    for (int b = 0; b < EqDisplay::kBands; ++b) resetBand (b);
    juce::String out = display.runSelfTest (failures);
    auto check = [&] (bool ok, const juce::String& what) { out << (ok ? "ok     " : "ECHEC  ") << what << "\n"; if (! ok) ++failures; };

    // pastille : clic sur son témoin = allume / coupe, et la bande est choisie
    auto& chip = *chips[5];
    const juce::Point<float> led { (float) chip.getHeight() * 0.4f, (float) chip.getHeight() * 0.5f };
    chip.mouseDown (fakeEvent (chip, led, led, 1, false));
    check (getParam (st, ids::peq ("On", 5)) > 0.5f && selected == 5, "pastille 6 : clic sur le temoin, bande allumee et choisie");
    // type : LOW CUT (4e case) ; pente : 36 dB/oct (3e case)
    const juce::Point<float> cell { typeSel.getWidth() * 3.5f / 6.0f, typeSel.getHeight() * 0.5f };
    typeSel.mouseDown (fakeEvent (typeSel, cell, cell, 1, false));
    check (juce::roundToInt (getParam (st, ids::peq ("Type", 5))) == PE::lowCut, "type : LOW CUT choisi");
    const juce::Point<float> sc { slopeSel.getWidth() * 2.5f / 4.0f, slopeSel.getHeight() * 0.5f };
    slopeSel.mouseDown (fakeEvent (slopeSel, sc, sc, 1, false));
    check (juce::roundToInt (getParam (st, ids::peq ("Slope", 5))) == 2, "pente : 36 dB/oct");
    // RESET BAND
    if (reset.onClick) reset.onClick();   // le clic du bouton (triggerClick passerait par la file de messages)
    const auto d = PE::defaultBand (5);
    check (getParam (st, ids::peq ("On", 5)) < 0.5f && juce::roundToInt (getParam (st, ids::peq ("Type", 5))) == d.type
           && std::abs (getParam (st, ids::peq ("Freq", 5)) - d.freq) < 1.0f, "RESET BAND : la bande revient a son point de depart");

    for (int b = 0; b < EqDisplay::kBands; ++b) resetBand (b);
    out << (failures == 0 ? "TOUT EST BON" : juce::String (failures) + " ECHEC(S)") << "\n";
    return out;
}
#endif
