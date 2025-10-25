#include "PluginEditor.h"
#include "FlipUtils.h"
#include <random>
#include "EngineDefs.h"
#include <map>
#include <vector>
#include <algorithm> // for std::find
#include <cstdint>  // for std::uint64_t
#include "DrumStyles.h" 
#include "BassStyleDB.h"
#include "DrumGridComponent.h"

using AP = juce::AudioProcessorValueTreeState;

juce::AudioProcessorEditor* BoomAudioProcessor::createEditor()
{
    return new BoomAudioProcessorEditor(*this);
}

// --- Safe APVTS read helper (put near top of file, anonymous namespace) ---
namespace {
    inline float readParam(const juce::AudioProcessorValueTreeState& s,
        const char* id, float fallback)
    {
        if (auto* v = s.getRawParameterValue(id)) return v->load(); // actual (un-normalised) value
        if (auto* p = s.getParameter(id)) return p->getValue(); // normalised 0..1 fallback
        return fallback;
    }
}

static AP::ParameterLayout createLayout()
{
    using AP = juce::AudioProcessorValueTreeState;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    // ---------- NEW: BPM Lock ----------
    p.push_back(std::make_unique<juce::AudioParameterBool>(
        "bpmLock",        // ID
        "BPM Lock",       // Name
        true              // default: locked (set false if you prefer unlocked by default)
    ));

    p.push_back(std::make_unique<juce::AudioParameterChoice>("engine", "Engine", boom::engineChoices(), (int)boom::Engine::Drums));
    p.push_back(std::make_unique<juce::AudioParameterChoice>("timeSig", "Time Signature", boom::timeSigChoices(), 0));
    p.push_back(std::make_unique<juce::AudioParameterChoice>("bars", "Bars", boom::barsChoices(), 0));

    p.push_back(std::make_unique<juce::AudioParameterFloat>("humanizeTiming", "Humanize Timing", juce::NormalisableRange<float>(0.f, 100.f), 0.f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("humanizeVelocity", "Humanize Velocity", juce::NormalisableRange<float>(0.f, 100.f), 0.f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("swing", "Swing", juce::NormalisableRange<float>(0.f, 100.f), 0.f));

    p.push_back(std::make_unique<juce::AudioParameterBool>("useTriplets", "Triplets", false));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("tripletDensity", "Triplet Density", juce::NormalisableRange<float>(0.f, 100.f), 0.f));
    p.push_back(std::make_unique<juce::AudioParameterBool>("useDotted", "Dotted Notes", false));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("dottedDensity", "Dotted Density", juce::NormalisableRange<float>(0.f, 100.f), 0.f));

    p.push_back(std::make_unique<juce::AudioParameterChoice>("key", "Key", boom::keyChoices(), 0));
    p.push_back(std::make_unique<juce::AudioParameterChoice>("scale", "Scale", boom::scaleChoices(), 0));
    p.push_back(std::make_unique<juce::AudioParameterChoice>("octave", "Octave", juce::StringArray("-2", "-1", "0", "+1", "+2"), 2));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("restDensity808", "Rest Density 808", juce::NormalisableRange<float>(0.f, 100.f), 10.f));

    p.push_back(std::make_unique<juce::AudioParameterChoice>("bassStyle", "Bass Style", boom::styleChoices(), 0));

    p.push_back(std::make_unique<juce::AudioParameterChoice>("drumStyle", "Drum Style", boom::styleChoices(), 0));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("restDensityDrums", "Rest Density Drums", juce::NormalisableRange<float>(0.f, 100.f), 5.f));

    p.push_back(std::make_unique<juce::AudioParameterInt>("seed", "Seed", 0, 1000000, 0));


    return { p.begin(), p.end() };
}

namespace {
    // Convert boom::drums::DrumPattern --> BoomAudioProcessor::Pattern
    static void copyDrumPattern(const boom::drums::DrumPattern& src,
        BoomAudioProcessor::Pattern& dst)
    {
        dst.clearQuick();
        dst.ensureStorageAllocated(src.size());
        for (const auto& e : src)      // NOTE: iterate the Array directly (no .events)
        {
            BoomAudioProcessor::Note n;
            n.row = e.row;
            n.startTick = e.startTick;
            n.lengthTicks = e.lenTicks;
            n.velocity = juce::jlimit<int>(1, 127, (int)e.vel);
            dst.add(n);
        }
    }
}

namespace
{
    inline int getPct(const juce::AudioProcessorValueTreeState& apvts, const char* id, int def = 0)
    {
        if (auto* p = apvts.getRawParameterValue(id))
            return juce::jlimit(0, 100, (int)juce::roundToInt(p->load()));
        return def;
    }

    inline int clampInt(int v, int lo, int hi)
    {
        return juce::jlimit(lo, hi, v);
    }
}

// ----- 808 Generator (bars: 1/2/4/8; density 0..100; honors style/key/scale/TS) -----
void BoomAudioProcessor::generate808(const juce::String& style,
    const juce::String& keyName,
    const juce::String& scaleName,
    int bars,
    int densityPercent,
    bool allowTriplets,
    bool allowDotted)
{
    // Only for melodic engine (non-Drums). If Drums, do nothing.
    if (getEngineSafe() == boom::Engine::Drums)
        return;

    // ---- Config & helpers ----
    bars = juce::jlimit(1, 8, bars);
    densityPercent = juce::jlimit(0, 100, densityPercent);

    // time signature -> steps per bar
    // Your grid = 24 ticks per 1/16 step.
    auto tsParam = apvts.state.getProperty("timeSig").toString(); // however you store it; fallback to "4/4"
    if (tsParam.isEmpty()) tsParam = "4/4";
    auto tsParts = juce::StringArray::fromTokens(tsParam, "/", "");
    const int tsNum = tsParts.size() >= 1 ? tsParts[0].getIntValue() : 4;
    const int tsDen = tsParts.size() >= 2 ? tsParts[1].getIntValue() : 4;

    auto stepsPerBarFor = [&](int num, int den)->int
    {
        // 4/4 -> 16 steps, 3/4 -> 12, 6/8 -> 12, 7/8 -> 14, etc.
        if (den == 4) return num * 4;
        if (den == 8) return num * 2;
        if (den == 16) return num; // very fine
        return num * 4;            // default
    };
    const int stepsPerBar = stepsPerBarFor(tsNum, tsDen);
    const int tps = 24;                         // ticks per 1/16 step (your grid)
    const int ppq = 96;                         // for export (not used here)
    const int totalSteps = stepsPerBar * bars;

    // ---- Scale tables ----
    static const std::map<juce::String, std::vector<int>, std::less<>> kScales = {
        { "Major",          { 0,2,4,5,7,9,11 } },
        { "Natural Minor",  { 0,2,3,5,7,8,10 } },
        { "Harmonic Minor", { 0,2,3,5,7,8,11 } },
        { "Dorian",         { 0,2,3,5,7,9,10 } },
        { "Phrygian",       { 0,1,3,5,7,8,10 } },
        { "Lydian",         { 0,2,4,6,7,9,11 } },
        { "Mixolydian",     { 0,2,4,5,7,9,10 } },
        { "Aeolian",        { 0,2,3,5,7,8,10 } },
        { "Locrian",        { 0,1,3,5,6,8,10 } },
        { "Blues",          { 0,3,5,6,7,10 } },
        { "Pentatonic Maj", { 0,2,4,7,9 } },
        { "Pentatonic Min", { 0,3,5,7,10 } },
        { "Chromatic",      { 0,1,2,3,4,5,6,7,8,9,10,11 } }
    };
    static const juce::StringArray kKeys = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

    auto keyIndex = juce::jmax(0, kKeys.indexOf(keyName.trim().toUpperCase()));
    const auto itScale = kScales.find(scaleName.trim());
    const auto& scalePCs = (itScale != kScales.end()) ? itScale->second : kScales.at("Chromatic");

    auto wrap12 = [](int v) { v %= 12; if (v < 0) v += 12; return v; };

    auto degreeToPitch = [&](int degree, int octave)->int
    {
        // degree is index in scalePCs (wrap), root = keyIndex
        const int pc = scalePCs[(degree % (int)scalePCs.size() + (int)scalePCs.size()) % (int)scalePCs.size()];
        return juce::jlimit(0, 127, octave * 12 + wrap12(keyIndex + pc));
    };

    // ---- Randomness (never same twice) ----
    // Mix in a monotonic nonce so even same UI values differ each time
// Robust seed: mix 32-bit millis, 64-bit hi-res ticks, and a monotonic nonce
    const auto now32 = juce::Time::getMillisecondCounter();      // uint32
    const auto ticks64 = (std::uint64_t)juce::Time::getHighResolutionTicks();
    const auto nonce = genNonce_.fetch_add(1, std::memory_order_relaxed) + 1; // or ++genNonce_ if not atomic

    const std::uint64_t mix = (std::uint64_t)now32
        ^ (ticks64)
        ^ (std::uint64_t)nonce;

    // keep it in signed-int range for juce::Random
    const int seed = (int)(mix & 0x7fffffff);
    juce::Random rng(seed);
    auto pct = [&](int prob)->bool { return rng.nextInt({ 100 }) < juce::jlimit(0, 100, prob); };

    // ---- Clear melodic pattern, we’re generating fresh 808s ----
    auto mp = getMelodicPattern();
    mp.clear();

    // Base octave and note length policy per style
    int baseOct = 3; // C3-ish base for 808
    int sustainStepsDefault = (tsDen == 8 ? 2 : 1); // default 1/8 in 4/4, 1/4 in 6/8 feel

    if (style.equalsIgnoreCase("trap") || style.equalsIgnoreCase("wxstie") || style.equalsIgnoreCase("drill"))
    {
        baseOct = 2;                 // lower 808s
        sustainStepsDefault = 1;     // staccato-ish, relies on repeated notes/rolls
    }

    // Density → how often we place notes per step
    auto placeProbability = [&](int step)->int
    {
        // swing accent: slightly more on offbeats for trap/drill
        int base = densityPercent;
        if (style.equalsIgnoreCase("trap") || style.equalsIgnoreCase("drill"))
            if ((step % 2) == 1) base = juce::jmin(100, base + 8);
        return base;
    };

    // Choose rhythmic subdivision for a “burst”
    auto chooseSubTick = [&]()
    {
        // 24 (1/16), 12 (1/32), 8 (~1/16T), 6 (fast), 4 (very fast)
        int pool[] = { 24, 12, 8, 6, 4 };
        int idx = rng.nextInt({ (int)std::size(pool) });
        int sub = pool[idx];
        if (!allowTriplets && sub == 8) sub = 12;           // avoid triplet-ish
        return sub;
    };

    // Decide dotted: multiply steps by 1.5
    auto dottedLen = [&](int steps)->int
    {
        if (!allowDotted) return steps;
        return steps + steps / 2; // *1.5
    };

    // Melodic motion choices by style (root, fifth, octave, neighbor tones)
    auto chooseDegreeDelta = [&]()
    {
        if (style.equalsIgnoreCase("trap"))
        {
            int r = rng.nextInt({ 100 });
            if (r < 40) return 0;   // root
            if (r < 65) return 4;   // fifth up
            if (r < 80) return -3;  // minor 3rd below (bluesy)
            if (r < 90) return 7;   // octave-ish (wrap by scale index)
            return (rng.nextBool() ? +1 : -1); // neighbor
        }
        else if (style.equalsIgnoreCase("drill"))
        {
            int r = rng.nextInt({ 100 });
            if (r < 35) return 0;
            if (r < 60) return 4;
            if (r < 75) return -2;  // lean into darker neighbor
            if (r < 90) return 7;
            return (rng.nextBool() ? +2 : -2);
        }
        else if (style.equalsIgnoreCase("wxstie"))
        {
            int r = rng.nextInt({ 100 });
            if (r < 45) return 0;
            if (r < 70) return 4;
            if (r < 85) return (rng.nextBool() ? +1 : -1);
            return 7;
        }
        else
        {
            // generic (pop/edm/rnb/rock/reggaeton, etc.)
            int r = rng.nextInt({ 100 });
            if (r < 50) return 0;
            if (r < 75) return 4;
            return (rng.nextBool() ? +1 : -1);
        }
    };

    // Build stepwise with bursts and sustains
    int currentDegree = 0;  // start on root
    int currentOct = baseOct;

    auto addNote = [&](int step, int lenSteps, int vel)
    {
        const int startTick = step * tps;
        const int lenTick = juce::jmax(6, lenSteps * tps);
        const int pitch = degreeToPitch(currentDegree, currentOct);
        mp.add({ pitch, startTick, lenTick, juce::jlimit(1,127,vel), 1 });
    };

    for (int step = 0; step < totalSteps; )
    {
        if (!pct(placeProbability(step))) { ++step; continue; }

        // Decide between a sustained note or a burst (roll)
        const bool doBurst = (style.equalsIgnoreCase("trap") || style.equalsIgnoreCase("drill")) ? pct(55) : pct(25);

        if (doBurst)
        {
            // Short rapid-fire notes in-place
            int sub = chooseSubTick();
            int durSteps = juce::jlimit(1, 4, 1 + rng.nextInt({ 3 }));
            int lenTickTotal = durSteps * tps;
            int t = step * tps;
            int endT = t + lenTickTotal;

            // small melodic wiggle inside the burst
            int localDeg = currentDegree;
            while (t < endT)
            {
                const int subTick = juce::jmax(3, juce::jmin(sub, endT - t));
                int v = 90 + rng.nextInt({ 25 });
                int pitch = degreeToPitch(localDeg, currentOct);
                mp.add({ pitch, t, subTick, juce::jlimit(1,127,v), 1 });

                // occasionally nudge degree
                if (pct(35)) localDeg += (rng.nextBool() ? +1 : -1);

                t += subTick;
            }

            step += durSteps; // advance by steps we consumed
            // occasional octave hop after bursts
            if (pct(20)) currentOct = juce::jlimit(1, 6, currentOct + (rng.nextBool() ? +1 : -1));
        }
        else
        {
            // Sustained hit, optionally dotted, then melodic move
            int lenSteps = sustainStepsDefault + rng.nextInt({ 2 });  // 1..2 (or 2..3 if default 2)
            if (pct(20)) lenSteps = dottedLen(lenSteps);

            addNote(step, lenSteps, 96 + rng.nextInt({ 20 }));
            step += lenSteps;

            // Move degree for next note
            currentDegree += chooseDegreeDelta();

            // Keep 808 in musical range
            if (pct(10)) currentOct = juce::jlimit(1, 6, currentOct + (rng.nextBool() ? +1 : -1));
        }
    }

    setMelodicPattern(mp);
    // repaint the UI (safe replacement for sendChangeMessage)
    if (auto* ed = getActiveEditor()) ed->repaint();
}



// ============================================================
// Bass Generator – rhythm-first, style-weighted, variety-safe.
// ============================================================

namespace
{
    // 96 ticks/quarter => 24 per 1/16 (you already use this grid elsewhere)
    constexpr int kTicksPerQuarter = 96;
    constexpr int kTicksPer16 = kTicksPerQuarter / 4;

    struct BassStyleSpec
    {
        // Weight maps per 1/16th within a 1-bar cell (size 16)
        // Higher number => more likely to place an onset there
        int weight16[16];

        // Probability (0..1) of subdividing a chosen onset into two 1/32s
        float splitTo32Prob;

        // Density scaler (0..1) used as baseline before restPct is applied
        float baseDensity;

        // Syncopation bias: + pushes off-beats, - pushes on-beats
        float syncBias; // -1..+1
    };

    // A tiny per-style table. You can extend/replace with your database easily.
    // We’re mapping by lowercase style name.
    BassStyleSpec getBassStyleSpec(const juce::String& styleLower)
    {
        // Defaults: “pop-ish”
        BassStyleSpec s = {
            /*weight16*/ { 9,2,4,2,  8,3,4,3,  9,2,4,2,  8,3,4,3 },
            /*split*/     0.10f,
            /*density*/   0.55f,
            /*sync*/      0.10f
        };

        if (styleLower == "trap")
        {
            BassStyleSpec t = {
                // On-beats + 8th feel + room for syncopation
                { 10,3,7,3,  9,3,7,3,  10,3,7,3,  9,3,7,3 },
                0.25f,  // occasional 1/32 “bop”
                0.65f,  // fairly dense
                0.15f
            };
            return t;
        }
        if (styleLower == "drill")
        {
            BassStyleSpec d = {
                // Triplet-heavy feel approximated by pushing 3,7,11,15
                { 7,3,9,2,  6,3,9,2,  7,3,9,2,  6,3,9,2 },
                0.30f,
                0.55f,
                0.25f  // more off-beat bias
            };
            return d;
        }
        if (styleLower == "wxstie")
        {
            BassStyleSpec w = {
                // Sparser + unpredictable; keep on-beats okay, push some off-beats
                { 10,2,5,2,  8,2,6,2,  10,2,5,2,  8,2,6,2 },
                0.20f,
                0.45f, // sparser baseline
                0.35f  // stronger syncopation preference
            };
            return w;
        }
        if (styleLower == "hip hop" || styleLower == "hiphop")
        {
            BassStyleSpec h = {
                { 10,2,4,2,  8,2,5,2,  10,2,4,2,  8,2,5,2 },
                0.12f,
                0.55f,
                0.05f
            };
            return h;
        }
        if (styleLower == "r&b" || styleLower == "rnb")
        {
            BassStyleSpec r = {
                // More space + occasional anticipations
                { 9,2,5,2,  7,2,5,2,  9,2,5,2,  7,2,5,2 },
                0.15f,
                0.45f,
                0.10f
            };
            return r;
        }
        if (styleLower == "edm")
        {
            BassStyleSpec e = {
                // Four-on-the-floor support w/ 8th push
                { 10,3,6,3,  9,3,6,3,  10,3,6,3,  9,3,6,3 },
                0.18f,
                0.60f,
                -0.05f  // bias *toward* on-beats
            };
            return e;
        }
        if (styleLower == "reggaeton")
        {
            BassStyleSpec g = {
                // DemBow-ish: emphasize 1, “and” of 2, and 4-ish positions
                { 11,2,5,2,  7,9,4,2,  10,2,5,2,  7,8,4,2 },
                0.12f,
                0.55f,
                0.20f
            };
            return g;
        }
        if (styleLower == "rock")
        {
            BassStyleSpec k = {
                // Straighter, on-beat support; fewer syncopations
                { 11,2,4,2,  9,2,4,2,  11,2,4,2,  9,2,4,2 },
                0.08f,
                0.50f,
                -0.10f
            };
            return k;
        }

        return s; // default
    }

    static inline float urand01(juce::Random& rng) { return rng.nextFloat(); }
}


void BoomAudioProcessor::aiStyleBlendDrums(const juce::String& styleA, const juce::String& styleB, int bars, float wA, float wB)
{
    // Normalize weights
    wA = std::max(0.0f, wA);
    wB = std::max(0.0f, wB);
    const float sum = (wA + wB > 0.0001f ? (wA + wB) : 1.0f);
    wA /= sum; wB /= sum;

    // Pick A vs B by weight
    std::random_device rd; std::mt19937 rng(rd());
    std::uniform_real_distribution<float> coin(0.0f, 1.0f);
    const juce::String chosen = (coin(rng) < wA ? styleA : styleB);

    // Pull global “feel” from sliders
    const int restPct = getPct(apvts, "restDensity", 0);
    const int dottedPct = getPct(apvts, "dottedDensity", 0);
    const int tripletPct = getPct(apvts, "tripletDensity", 0);
    const int swingPct = getPct(apvts, "swing", 0);

    // Generate drums via your style DB
    auto styles = boom::drums::styleNames();
    if (!styles.contains(chosen))
        return; // unknown style; bail quietly

    boom::drums::DrumStyleSpec spec = boom::drums::getSpec(chosen);
    boom::drums::DrumPattern pat;
    boom::drums::generate(spec, bars, restPct, dottedPct, tripletPct, swingPct, /*seed*/ -1, pat);

    // Convert DB pattern -> processor’s DrumNote array
    BoomAudioProcessor::Pattern out;
    out.ensureStorageAllocated(pat.size());
    for (const auto& e : pat)
    {
        BoomAudioProcessor::Note n;
        n.row = e.row;
        n.startTick = e.startTick;
        n.lengthTicks = e.lenTicks;
        n.velocity = juce::jlimit<int>(1, 127, (int)e.vel);
        out.add(n);
    }
    setDrumPattern(out);
}

void BoomAudioProcessor::aiSlapsmithExpand(int bars)
{
    // Use current style name if you store it in APVTS choice "style". If not present, default to "trap".
    juce::String baseStyle = "trap";
    if (auto* p = apvts.getParameter("style"))
        baseStyle = static_cast<juce::AudioParameterChoice*>(p)->getCurrentChoiceName();

    // Read sliders
    const int restPct = clampInt(getPct(apvts, "restDensity", 0) - 10, 0, 100); // slightly fewer rests
    const int dottedPct = getPct(apvts, "dottedDensity", 0);
    int       tripletPct = getPct(apvts, "tripletDensity", 0);
    const int swingPct = getPct(apvts, "swing", 0);
    if (baseStyle.equalsIgnoreCase("drill")) tripletPct = clampInt(tripletPct + 10, 0, 100);

    // Generate a fresh embellishment
    auto styles = boom::drums::styleNames();
    if (!styles.contains(baseStyle))
        baseStyle = styles[0];

    boom::drums::DrumStyleSpec spec = boom::drums::getSpec(baseStyle);
    boom::drums::DrumPattern pat;
    boom::drums::generate(spec, bars, restPct, dottedPct, tripletPct, swingPct, /*seed*/ -1, pat);

    // Merge with existing by simply replacing (simplest/robust). If you want true "expand", merge selectively.
    BoomAudioProcessor::Pattern out;
    out.ensureStorageAllocated(pat.size());
    for (const auto& e : pat)
    {
        BoomAudioProcessor::Note n;
        n.row = e.row;
        n.startTick = e.startTick;
        n.lengthTicks = e.lenTicks;
        n.velocity = juce::jlimit<int>(1, 127, (int)e.vel);
        out.add(n);
    }
    setDrumPattern(out);
}

void BoomAudioProcessor::randomizeCurrentEngine(int bars)
{
    // We’ll randomize slider/choice style and generate **drums**. (Bass/808 can be added after you confirm names)
    std::random_device rd; std::mt19937 rng(rd());

    // Randomize style if you have a "style" parameter (AudioParameterChoice)
    if (auto* prm = apvts.getParameter("style"))
    {
        auto* ch = static_cast<juce::AudioParameterChoice*>(prm);
        if (ch->choices.size() > 0)
        {
            const int idx = std::uniform_int_distribution<int>(0, ch->choices.size() - 1)(rng);
            ch->operator=(idx);
        }
    }

    // Randomize density sliders if present
    auto randPct = [&](const char* id, int lo, int hi)
    {
        if (auto* r = apvts.getRawParameterValue(id))
        {
            const int v = std::uniform_int_distribution<int>(lo, hi)(rng);
            r->operator=((float)v);
        }
    };
    randPct("restDensity", 0, 60);
    randPct("dottedDensity", 0, 40);
    randPct("tripletDensity", 0, 60);
    randPct("swing", 0, 40);

    // Generate drums for the randomized style
    juce::String style = "trap";
    if (auto* prm = apvts.getParameter("style"))
        style = static_cast<juce::AudioParameterChoice*>(prm)->getCurrentChoiceName();

    auto styles = boom::drums::styleNames();
    if (!styles.contains(style) && styles.size() > 0)
        style = styles[0];

    const int restPct = getPct(apvts, "restDensity", 0);
    const int dottedPct = getPct(apvts, "dottedDensity", 0);
    const int tripletPct = getPct(apvts, "tripletDensity", 0);
    const int swingPct = getPct(apvts, "swing", 0);


    boom::drums::DrumStyleSpec spec = boom::drums::getSpec(style);
    boom::drums::DrumPattern pat;
    boom::drums::generate(spec, bars, restPct, dottedPct, tripletPct, swingPct, /*seed*/ -1, pat);

    BoomAudioProcessor::Pattern out;
    out.ensureStorageAllocated(pat.size());
    for (const auto& e : pat)
    {
        BoomAudioProcessor::Note n;
        n.row = e.row;
        n.startTick = e.startTick;
        n.lengthTicks = e.lenTicks;
        n.velocity = juce::jlimit<int>(1, 127, (int)e.vel);
        out.add(n);
    }
    setDrumPattern(out);
}

void BoomAudioProcessor::generateBassFromSpec(const juce::String& styleName,
    int bars,
    int octave,
    int restPct,
    int dottedPct,
    int tripletPct,
    int swingPct,
    int seed)
{
    // ---- Safety / setup ----
    const int barsClamped = juce::jlimit(1, 16, bars);
    const int total16 = barsClamped * 16;
    const int tpq = kTicksPerQuarter;
    const int ticksPer16 = kTicksPer16;

    // Variety seed
    juce::Random rng = (seed >= 0)
        ? juce::Random((int)seed)
        : juce::Random((int)(juce::Time::getMillisecondCounter() ^ (uintptr_t)this));

    // Pull per-style spec (rhythm weights, biases)
    auto spec = getBassStyleSpec(styleName.trim().toLowerCase());

    // Convert UI percents to [0..1]
    const float restF = juce::jlimit(0.f, 1.f, restPct / 100.f);
    const float dottedF = juce::jlimit(0.f, 1.f, dottedPct / 100.f);
    const float tripletF = juce::jlimit(0.f, 1.f, tripletPct / 100.f);
    const float swingF = juce::jlimit(0.f, 1.f, swingPct / 100.f);

    // Effective density: base scaled by (1 - rest)
    const float density = juce::jlimit(0.f, 1.f, spec.baseDensity * (1.0f - restF));

    // Prepare pattern out (use your existing melodic note struct)
    auto pat = getMelodicPattern();
    pat.clearQuick();

    // Choose a single nominal pitch line (you said rhythm-first; keep melody minimal)
    // Root pitch from key/scale can be fetched if you have it; here we anchor by octave.
    // Middle C=60; octave offset => 12 * (octave - 4)
    const int basePitch = 36 + (octave * 12); // C2 as base for octave=0; tweak if needed

    // Build a per-16th probability table for the whole region with syncopation bias
    juce::Array<float> prob16; prob16.resize(total16);
    for (int i = 0; i < total16; ++i)
    {
        const int p = i % 16;
        float w = (float)spec.weight16[p];

        // bias off-beats if syncBias > 0, bias on-beats if < 0
        const bool onBeat = (p % 4 == 0);
        w += (onBeat ? -1.f : +1.f) * (std::abs(spec.syncBias) * 3.0f);

        // dotted/triplet influence: gently nudge certain slots
        if (dottedF > 0.05f)
        {
            // dotted 8th (3x 1/16) emphasis -> positions 3, 7, 11, 15
            if (p == 3 || p == 7 || p == 11 || p == 15) w += 2.0f * dottedF;
        }
        if (tripletF > 0.05f)
        {
            // crude triplet feel bump near 1/3 and 2/3 of a beat: positions 1,5,9,13 and 3,7,11,15
            if (p == 1 || p == 5 || p == 9 || p == 13) w += 1.6f * tripletF;
            if (p == 3 || p == 7 || p == 11 || p == 15) w += 1.2f * tripletF;
        }

        prob16.set(i, juce::jmax(0.0f, w));
    }

    // Normalize-ish and then sample per 16th based on density
    // Target hits ~ density * total16 * 0.6 (tunable)
    const int targetHits = juce::jmax(1, (int)std::round(density * total16 * 0.6f));

    // Build a weighted index bag
    juce::Array<int> idx; idx.ensureStorageAllocated(total16);
    float sumW = 0.f;
    for (int i = 0; i < total16; ++i) sumW += prob16[i];
    if (sumW <= 0.001f) sumW = 1.0f;

    // Greedy sampling without replacement
    juce::Array<bool> taken; taken.resize(total16); for (int i = 0; i < total16; ++i) taken.set(i, false);

    auto drawIndex = [&]()
    {
        float r = urand01(rng) * sumW;
        for (int i = 0; i < total16; ++i)
        {
            if (taken[i]) continue;
            r -= prob16[i];
            if (r <= 0.0f)
                return i;
        }
        // fallback linear search for first available
        for (int i = 0; i < total16; ++i)
            if (!taken[i]) return i;
        return 0;
    };

    for (int k = 0; k < targetHits; ++k)
    {
        const int i = drawIndex();
        taken.set(i, true);
        sumW -= prob16[i];
        idx.add(i);
    }

    // Convert chosen 16th slots -> note events. Length rules:
    // - default: 1/8 long (2 * 1/16)
    // - occasionally tie two 1/16s (dotted feel)
    // - occasionally split into 1/32 pair
    for (int i : idx)
    {
        const int startTick = i * ticksPer16;
        int lenTicks = 2 * ticksPer16; // 1/8 default

        // dotted bias (extend by 1/16) based on dottedF
        if (urand01(rng) < (0.25f * dottedF))
            lenTicks += ticksPer16;

        // occasional split into two 1/32 hits
        const bool split32 = (urand01(rng) < spec.splitTo32Prob * (0.5f + 0.5f * (1.0f - restF)));
        if (!split32)
        {
            // single note
            pat.add({ basePitch, startTick, lenTicks, 100, 1 });
        }
        else
        {
            const int hit32 = ticksPer16 / 2; // 1/32 ticks
            pat.add({ basePitch, startTick, hit32, 100, 1 });
            const int start2 = startTick + hit32 + (rng.nextBool() ? 0 : hit32); // sometimes a little gap
            pat.add({ basePitch, start2, hit32, 96, 1 });
        }
    }

    // Apply swing (timing offset on even 1/8s). Simple micro-timing at tick level.
    if (swingF > 0.01f)
    {
        const int swingTicksMax = (int)std::round(0.5 * kTicksPer16 * swingF); // up to 50% of a 16th
        for (int n = 0; n < pat.size(); ++n)
        {
            auto note = pat[n];
            const int sixteenth = note.startTick / kTicksPer16;
            const bool isEven8th = ((sixteenth / 2) % 2) == 1; // 0,1 -> 8th#1, 2,3 -> 8th#2, etc.
            if (isEven8th)
            {
                note.startTick = juce::jmax(0, note.startTick + swingTicksMax);
                pat.set(n, note);
            }
        }
    }

    // Commit to processor state
    setMelodicPattern(pat);
    // If you have a change-broadcast mechanism, ping it here so the editor refreshes.
    // sendChangeMessage(); // only if you already use it.
}



BoomAudioProcessor::BoomAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "PARAMS", createLayout())
{
}


void BoomAudioProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    juce::MemoryOutputStream mos(dest, true);
    apvts.copyState().writeToStream(mos);
}

void BoomAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto vt = juce::ValueTree::readFromData(data, (size_t)sizeInBytes); vt.isValid())
        apvts.replaceState(vt);
}


// Notify the active editor that data changed (safe replacement for sendChangeMessage)
static inline void notifyEditor(BoomAudioProcessor& ap)
{
    if (auto* ed = ap.getActiveEditor())
        ed->repaint();
}



void BoomAudioProcessor::bumpDrumRowsUp()
{
    auto pat = getDrumPattern();
    if (pat.size() == 0) { notifyPatternChanged(); return; }

    // find max row index present
    int maxRow = 0;
    for (auto& n : pat) maxRow = std::max(maxRow, n.row);

    for (auto& n : pat)
        n.row = (n.row + 1) % (maxRow + 1);

    setDrumPattern(pat);
    notifyPatternChanged();
}

namespace
{
    // ---- Scale tables (semitones from root). Add more as you wish. ----
    // These cover common choices and won’t block your build if a rare name is passed.
    static const std::map<juce::String, std::vector<int>, std::less<>> kScales = {
    {"Major",         {0, 2, 4, 5, 7, 9, 11}},
    {"Natural Minor", {0, 2, 3, 5, 7, 8, 10}},
    {"Harmonic Minor",{0, 2, 3, 5, 7, 8, 11}},
    {"Dorian",        {0, 2, 3, 5, 7, 9, 10}},
    {"Phrygian",      {0, 1, 3, 5, 7, 8, 10}},
    {"Lydian",        {0, 2, 4, 6, 7, 9, 11}},
    {"Mixolydian",    {0, 2, 4, 5, 7, 9, 10}},
    {"Aeolian",       {0, 2, 3, 5, 7, 8, 10}},
    {"Locrian",       {0, 1, 3, 5, 6, 8, 10}},
    {"Locrian Nat6",    {0, 1, 3, 5, 6, 9, 10}},
    {"Ionian #5",     {0, 2, 4, 6, 7, 9, 11}},
    {"Dorian #4",     {0, 2, 3, 6, 7, 9, 10}},
    {"Phrygian Dom",  {0, 1, 3, 5, 7, 9, 10}},
    {"Lydian #2",     {0, 3, 4, 6, 7, 9, 11}},
    {"Super Locrian", {0, 1, 3, 4, 6, 8, 10}},
    {"Dorian b2",     {0, 1, 3, 5, 7, 9, 10}},
    {"Lydian Aug",    {0, 2, 4, 6, 8, 9, 11}},
    {"Lydian Dom",    {0, 2, 4, 6, 7, 9, 10}},
    {"Mixo b6",       {0, 2, 4, 5, 7, 8, 10}},
    {"Locrian #2",    {0, 2, 3, 5, 6, 8, 10}},
    {"8 Tone Spanish", {0, 1, 3, 4, 5, 6, 8, 10}},
    {"Phyrgian Nat3",    {0, 1, 4, 5, 7, 8, 10}},
    {"Blues",         {0, 3, 5, 6, 7, 10}},
    {"Hungarian Min", {0, 3, 5, 8, 11}},
    {"Harmonic Maj(Ethopian)",  {0, 2, 4, 5, 7, 8, 11}},
    {"Dorian b5",     {0, 2, 3, 5, 6, 9, 10}},
    {"Phrygian b4",   {0, 1, 3, 4, 7, 8, 10}},
    {"Lydian b3",     {0, 2, 3, 6, 7, 9, 11}},
    {"Mixolydian b2", {0, 1, 4, 5, 7, 9, 10}},
    {"Lydian Aug2",   {0, 3, 4, 6, 8, 9, 11}},
    {"Locrian bb7",   {0, 1, 3, 5, 6, 8, 9}},
    {"Pentatonic Maj", {0, 2, 5, 7, 8}},
    {"Pentatonic Min", {0, 3, 5, 7, 10}},
    {"Neopolitan Maj", {0, 1, 3, 5, 7, 9, 11}},
    {"Neopolitan Min", {0, 1, 3, 5, 7, 8, 10}},
    {"Spanish Gypsy",  {0, 1, 4, 5, 7, 8, 10}},
    {"Romanian Minor", {0, 2, 3, 6, 7, 9, 10}},
    {"Chromatic",      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
    {"Bebop Major",  {0, 2, 4, 5, 7, 8, 9, 11}},
    {"Bebop Minor", {0, 2, 3, 5, 7, 8, 9, 10}}, };


    inline int wrap12(int v) { v %= 12; if (v < 0) v += 12; return v; }

    // Snap a MIDI pitch to the nearest pitch in (root + scale). Tie breaks upward.
    inline int snapToScale(int midiPitch, int rootPC, const std::vector<int>& scalePCs)
    {
        const int pc = wrap12(midiPitch);
        // Already in scale? keep it
        for (int s : scalePCs) if (wrap12(rootPC + s) == pc) return midiPitch;

        // Otherwise search nearest distance in semitones up/down (0..6)
        for (int d = 1; d <= 6; ++d)
        {
            // up
            if (std::find(scalePCs.begin(), scalePCs.end(), wrap12(pc - rootPC + d)) != scalePCs.end())
                return midiPitch + d;
            // down
            if (std::find(scalePCs.begin(), scalePCs.end(), wrap12(pc - rootPC - d)) != scalePCs.end())
                return midiPitch - d;
        }
        return midiPitch; // fallback (shouldn’t happen with Chromatic present)
    }
}

void BoomAudioProcessor::transposeMelodic(int semitones, const juce::String& /*newKey*/,
    const juce::String& /*newScale*/, int octaveOffset)
{
    auto pat = getMelodicPattern();
    for (auto& n : pat)
        n.pitch = juce::jlimit(0, 127, n.pitch + semitones + 12 * octaveOffset);

    setMelodicPattern(pat);
    notifyPatternChanged();
}

void BoomAudioProcessor::bumppitTranspose(int targetKeyIndex, const juce::String& scaleName, int octaveDelta)
{
    // Only act if 808 or Bass engine is selected
    auto eng = getEngineSafe();
    // Melodic-only: proceed for any non-Drums engine (covers 808 and Bass without naming them)
    if (eng == boom::Engine::Drums)
        return;

    // Guard inputs
    targetKeyIndex = juce::jlimit(0, 11, targetKeyIndex);
    octaveDelta = juce::jlimit(-4, 4, octaveDelta);

    // Find scale intervals; default to Chromatic if unknown
    const std::vector<int>* scale = nullptr;
    auto it = kScales.find(scaleName.trim());
    if (it != kScales.end()) scale = &it->second;
    if (scale == nullptr)   scale = &kScales.at("Chromatic");

    // Transpose every melodic note to (root + scale), keep rhythm/length/velocity.
    // We’ll do: (a) octave shift, (b) snap to target scale relative to chosen key.
    auto mp = getMelodicPattern();

    for (auto& n : mp)
    {
        // 1) octave
        int pitch = n.pitch + (octaveDelta * 12);

        // 2) snap to scale rooted at targetKeyIndex
        pitch = snapToScale(pitch, targetKeyIndex, *scale);

        // Keep safe MIDI range
        n.pitch = juce::jlimit(0, 127, pitch);
    }

    setMelodicPattern(mp);
    notifyEditor(*this);
}

void BoomAudioProcessor::sendUIChange()
{
    // if you’re already an AudioProcessorValueTreeState::Listener or using ChangeBroadcaster,
    // this can be your change call. Otherwise, have the editor poll. For now:
    // (No-op is safe; editor will pull on timer. Keep method so calls compile.)
}

void BoomAudioProcessor::notifyPatternChanged()
{
    sendUIChange();
}

void BoomAudioProcessor::generate808(int bars,
    int keyIndex,
    const juce::String& scaleName,
    int octave,
    int restPct,
    int dottedPct,
    int tripletPct,
    int swingPct,
    int seed)
{
    juce::ignoreUnused(swingPct); // (hook swing later if needed)

    // ----- Prep RNG -----
    const uint32_t rngSeed = (seed == -1)
        ? static_cast<uint32_t>(juce::Time::getMillisecondCounter())
        : static_cast<uint32_t>(seed);
    std::mt19937 rng(rngSeed);
    auto rand01 = [&]() -> float {
        return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
    };
    auto chance = [&](float p01) { return rand01() < juce::jlimit(0.0f, 1.0f, p01); };

    // ----- Timing constants (96 PPQ) -----
    const int ppq = 96;
    const int beat = ppq;         // 1/4
    const int e8 = ppq / 2;     // 1/8
    const int e16 = ppq / 4;     // 1/16
    const int barT = 4 * ppq;
    const int totalT = bars * barT;

    // ----- Scale helper (root + degree picker) -----
    // We’ll map keyIndex (0=C .. 11=B) to MIDI, and derive a scale set by name.
    // Your project already has all scale names; here we just build degrees quickly.
    static const int semis[12] = { 0,1,2,3,4,5,6,7,8,9,10,11 }; // 0=C
    const int rootMidi = 12 * (juce::jlimit(-1, 9, octave) + 5) + juce::jlimit(0, 11, keyIndex); // center-ish

    auto makeScaleSemis = [&](const juce::String& nm) -> std::vector<int>
    {
        auto n = nm.trim().toLowerCase();
        // Basic sets (quick but effective); you already expose many modes in the UI
        if (n.contains("minor") || n == "aeolian")
            return { 0,2,3,5,7,8,10 };
        if (n == "dorian")
            return { 0,2,3,5,7,9,10 };
        if (n == "phrygian")
            return { 0,1,3,5,7,8,10 };
        if (n == "lydian")
            return { 0,2,4,6,7,9,11 };
        if (n == "mixolydian")
            return { 0,2,4,5,7,9,10 };
        if (n == "locrian")
            return { 0,1,3,5,6,8,10 };
        if (n.contains("blues"))
            return { 0,3,5,6,7,10 };
        // default major/ionian
        return { 0,2,4,5,7,9,11 };
    };

    const auto scaleDegrees = makeScaleSemis(scaleName);

    auto degreeMidi = [&](int degree) -> int
    {
        const int d = juce::jlimit(0, (int)scaleDegrees.size() - 1, degree);
        const int sm = scaleDegrees[d];
        return rootMidi + sm;
    };

    auto nearestScaleBelow = [&](int midi) -> int
    {
        // Walk down by semitone until we hit a scale tone modulo 12
        for (int m = midi; m >= midi - 24; --m)
        {
            int mod = (m - rootMidi) % 12; if (mod < 0) mod += 12;
            for (int sd : scaleDegrees) if (sd == mod) return m;
        }
        return midi;
    };

    // ----- PATH 1: rhythmic family selection -----
    enum class GridFamily { Eighth, Quarter, EighthTrip, QuarterTripRare, SixteenthRare };
    GridFamily family;
    {
        // weighted pick: 8th most common, then 1/4, 8T, 1/4T rare, 1/16 rare
        float r = rand01();
        if (r < 0.45f)      family = GridFamily::Eighth;
        else if (r < 0.75f) family = GridFamily::Quarter;
        else if (r < 0.92f) family = GridFamily::EighthTrip;
        else if (r < 0.97f) family = GridFamily::QuarterTripRare;
        else                family = GridFamily::SixteenthRare;
    }

    int grid = e8; // default 1/8
    switch (family)
    {
    case GridFamily::Quarter:         grid = beat; break;
    case GridFamily::Eighth:          grid = e8;   break;
    case GridFamily::EighthTrip:      grid = (barT / 12); break; // 12 per bar
    case GridFamily::QuarterTripRare: grid = (barT / 6);  break; // 6 per bar
    case GridFamily::SixteenthRare:   grid = e16;  break;
    }

    // ----- PATH 2: note length / spacing character -----
    const bool longNotes = chance(0.55f); // long sustain vs short & gappy

    // Rest density influence (0..100): higher = more holes
    const float restP = juce::jlimit(0, 100, restPct) / 100.0f;

    // ----- PATH 3: root-only vs chord-root walking -----
    const bool rootCentric = chance(0.65f); // 65% root heavy
    const bool chordWalk = !rootCentric;

    // ----- PATH 4: simple vs complex (fills, rolls, risks) -----
    const bool complex = chance(0.45f);

    // Triplet/dotted influence multipliers (push probabilities a bit)
    const float tripInfl = juce::jlimit(0, 100, tripletPct) / 100.0f;
    const float dotInfl = juce::jlimit(0, 100, dottedPct) / 100.0f;

    // ----- Build pattern -----
    auto melodic = getMelodicPattern();
    melodic.clearQuick();

    auto addNote = [&](int start, int len, int midi, int vel)
    {
        start = juce::jlimit(0, totalT - 1, start);
        len = juce::jlimit(e16 / 2, barT, len);

        // Works for juce::Array of your note struct (pitch,startTick,lengthTicks,velocity,channel)
        melodic.add({ juce::jlimit(0, 127, midi),
                      start,
                      len,
                      juce::jlimit(1, 127, vel),
                      1 }); // channel
    };

    // Choose base pitch strategy
    // root-centric: mostly root, sometimes 5th & passing tones
    // chord-walk: move target every 1/2 or 1 bar (roots/5ths/other scale tones)
    int targetDegree = 0; // 0=root in our degree vector
    int targetMidi = degreeMidi(targetDegree);

    auto chooseChordRoot = [&]() {
        // every switch picks from {root, 5th, other} with bias
        float r = rand01();
        if (r < 0.55f) { targetDegree = 0; }                    // root
        else if (r < 0.80f) { targetDegree = 4 % (int)scaleDegrees.size(); } // 5th
        else { // another scale tone
            int k = (int)(rand01() * scaleDegrees.size());
            targetDegree = juce::jlimit(0, (int)scaleDegrees.size() - 1, k);
        }
        targetMidi = degreeMidi(targetDegree);
    };

    // Walk schedule if chordWalk
    const int switchSpan = chance(0.5f) ? (barT / 2) : (barT); // 1/2 bar or 1 bar
    int nextSwitchAt = switchSpan;

    // Iterate the grid; place notes with variety
    for (int t = 0; t < totalT; t += grid)
    {
        if (chordWalk && t >= nextSwitchAt)
        {
            chooseChordRoot();
            nextSwitchAt += switchSpan;
        }

        // rest gating
        if (chance(restP)) continue;

        // Base pitch:
        int baseMidi = rootCentric ? degreeMidi(0) : targetMidi;

        // Optionally hop to 5th/other with ~35% chance when not root-centric
        if (!rootCentric && chance(0.35f))
        {
            if (chance(0.50f))
            {
                // Perfect 5th above (keep under 120)
                baseMidi = nearestScaleBelow(baseMidi + 7);
            }
            else
            {
                // some other degree
                int d = (int)(rand01() * scaleDegrees.size());
                baseMidi = degreeMidi(d);
            }
        }

        // Length logic
        int len = longNotes ? juce::jmax(grid, e8) : juce::jmax(e16, grid / 2);

        // Rolls or embellishments if complex
        if (complex && (family == GridFamily::Eighth || family == GridFamily::EighthTrip))
        {
            // sometimes split grid into two/three smaller pulses (trap-ish rolls)
            if (chance(0.25f + 0.25f * tripInfl))
            {
                const int sub = (family == GridFamily::EighthTrip ? grid / 3 : grid / 2);
                const int reps = (family == GridFamily::EighthTrip ? 3 : 2);
                for (int i = 0; i < reps; ++i)
                {
                    int vel = 88 + (int)(rand01() * 35.0f) - (i * 4);
                    addNote(t + i * sub, juce::jmax(e16, sub - e16 / 2), baseMidi, vel);
                }
                continue;
            }
        }

        // Occasional dotted push (offset) if dotted influence is high
        int start = t;
        if (chance(0.15f * dotInfl))
            start += e16; // nudge right a 16th

        // Place primary note
        int vel = 95 + (int)(rand01() * 28.0f);
        addNote(start, len, baseMidi, vel);

        // Rare pick-up before the beat (808 grace)
        if (complex && chance(0.12f))
        {
            int pu = juce::jmax(0, start - e16);
            addNote(pu, e16, nearestScaleBelow(baseMidi), juce::jlimit(60, 100, vel - 20));
        }
    }

    // Commit to processor + notify UI
    setMelodicPattern(melodic);
}

void BoomAudioProcessor::generateBass(int bars)
{
    // For now, use same generator as 808 (different velocity range and fewer long holds)
    auto pat = getMelodicPattern();
    pat.clear();

    const int total16 = q16(bars);
    int baseMidi = 40 + irand(0, 6); // slightly higher than 808

    const int hitPct = 50 + irand(-10, 10);

    for (int i = 0; i < total16; ++i)
    {
        const bool strong = (i % 4 == 0);
        if (strong || chance(hitPct))
        {
            int len16 = strong ? (chance(35) ? 3 : 2) : 1;
            int vel = strong ? irand(90, 110) : irand(75, 100);
            int pitch = baseMidi;

            if (!strong && chance(35))
            {
                static const int steps[] = { -2, 0, 2, 3, 5 };
                pitch += steps[irand(0, (int)std::size(steps) - 1)];
            }

            pat.add({ pitch, toTick16(i), toTick16(juce::jmax(1, len16)), vel, 1 });
            i += (len16 - 1);
        }
    }

    setMelodicPattern(pat);
    notifyPatternChanged();
}

// Build a probability mask from the current kick pattern (row 0) at 16th-note resolution.
// Returns a vector<int> length = bars*16, values in [0..100] = probability boost.
static std::vector<int> buildKickBiasMask(const BoomAudioProcessor& proc, int bars)
{
    const int total16 = proc.q16(bars);
    std::vector<int> bias(total16, 0);

    const auto drums = proc.getDrumPattern();
    for (const auto& n : drums)
    {
        if (n.row != 0) continue; // row 0 assumed = Kick
        const int start16 = (n.startTick * 4) / BoomAudioProcessor::PPQ;
        const int len16 = juce::jmax(1, (n.lengthTicks * 4) / BoomAudioProcessor::PPQ);
        for (int s = 0; s < len16; ++s)
        {
            const int idx = start16 + s;
            if ((unsigned)idx < bias.size())
                bias[idx] = juce::jlimit(0, 100, bias[idx] + juce::jmap(n.velocity, 1, 127, 15, 45));
        }

        // also nudge neighbors (so we "tend" toward kicks without being locked)
        const int pre = start16 - 1;
        const int pst = start16 + len16;
        if (pre >= 0)              bias[pre] = juce::jmax(bias[pre], 12);
        if (pst < (int)bias.size()) bias[pst] = juce::jmax(bias[pst], 12);
    }

    // tiny baseline so an empty kick lane still generates:
    for (auto& b : bias) b = juce::jmax(b, 6);
    return bias;
}




void BoomAudioProcessor::generateDrumRolls(const juce::String& style, int bars)
{
    auto pat = getDrumPattern();
    pat.clear();

    const int total16 = q16(bars);

    auto add = [&](int row, int step16, int len16, int vel)
    {
        pat.add({ 0, row, toTick16(step16), toTick16(len16), vel });
    };

    // We’ll target the snare row = 1 for “rolls”; add supportive hats row=2
    int rollRow = 1, hatRow = 2;

    if (style.compareIgnoreCase("trap") == 0)
    {
        // dense cresc/dim + 32nd trills sprinkled
        for (int b = 0; b < bars; ++b)
        {
            const int start = b * 16;
            for (int i = 0; i < 16; ++i)
            {
                if (i % 2 == 0 || chance(35)) add(hatRow, start + i, 1, irand(60, 85));
                if (chance(25))          add(rollRow, start + i, 1, irand(80, 110));
                if (chance(10) && i < 15)  add(rollRow, start + i + 1, 1, irand(70, 95)); // 32nd-feel echo
            }
        }
    }
    else if (style.compareIgnoreCase("drill") == 0)
    {
        // triplet-y rolls & late snares (beat 4 of bar 2/4 feel)
        for (int b = 0; b < bars; ++b)
        {
            const int start = b * 16;
            // strong “late” accent near beat 4 (index 12)
            add(rollRow, start + 12, 1, irand(100, 120));
            for (int t = 0; t < 6; ++t) // triplet grid approx (16th-triplet-ish)
            {
                int p = start + (t * 2);
                if (p < start + 16 && chance(60)) add(rollRow, p, 1, irand(75, 100));
            }
            for (int i = 0; i < 16; ++i)
                if (i % 2 == 0 || chance(20)) add(hatRow, start + i, 1, irand(60, 85));
        }
    }
    else if (style.compareIgnoreCase("edm") == 0)
    {
        // classic build: 1/4 -> 1/8 -> 1/16 -> 1/32 across bars
        int seg = bars * 16;
        int a = seg / 4, b = seg / 4, c = seg / 4, d = seg - (a + b + c);
        auto stamp = [&](int every16, int begin, int count)
        {
            for (int i = 0; i < count; i++)
            {
                int base = begin + i * every16;
                if (base < total16) add(rollRow, base, 1, irand(85, 115));
            }
        };
        stamp(4, 0, a / 4);              // quarters
        stamp(2, a, b / 2);              // eighths
        stamp(1, a + b, c);              // sixteenths
        for (int i = 0; i < d; ++i)        // 32nd-ish final rush: double up
        {
            add(rollRow, a + b + c + i, 1, irand(95, 120));
            if (chance(70) && a + b + c + i + 1 < total16) add(rollRow, a + b + c + i + 1, 1, irand(85, 110));
        }
    }
    else if (style.compareIgnoreCase("wxstie") == 0)
    {
        // sparser + choppy accents
        for (int b = 0; b < bars; ++b)
        {
            int start = b * 16;
            for (int i = 0; i < 16; ++i)
                if (chance(30)) add(rollRow, start + i, 1, irand(80, 110));
            for (int q = 0; q < 4; ++q)
                if (chance(60)) add(hatRow, start + q * 4, 1, irand(70, 95));
        }
    }
    else // pop / r&b / rock / reggaeton – tasteful single-bar fills
    {
        for (int b = 0; b < bars; ++b)
        {
            int start = b * 16;
            for (int i = 0; i < 16; ++i)
                if (chance(25)) add(rollRow, start + i, 1, irand(80, 105));
            // end-bar push
            add(rollRow, start + 14, 1, irand(95, 115));
            add(rollRow, start + 15, 1, irand(95, 115));
        }
    }

    setDrumPattern(pat);
    notifyPatternChanged();
}

void BoomAudioProcessor::generateDrums(int bars)
{
    auto pat = getDrumPattern();
    pat.clear();

    const int total16 = q16(bars);

    // row mapping: 0=Kick, 1=Snare, 2=Hat (match your DrumGrid row order)
    auto add = [&](int row, int step16, int len16, int vel)
    {
        pat.add({ 0 /*chan*/, row, toTick16(step16), toTick16(len16), vel });
    };

    // backbeat snares & variations
    for (int b = 0; b < bars; ++b)
    {
        int s = b * 16 + 8; // beat 3
        add(1, s, 1, irand(95, 115));
        if (chance(25)) add(1, s - 1, 1, irand(70, 90)); // flam-ish before
        if (chance(20)) add(1, s + 1, 1, irand(70, 90)); // after-beat
    }

    // kicks: downbeats + syncopation
    for (int i = 0; i < total16; i += 4)
    {
        if (i % 16 == 0 || chance(55)) add(0, i, 1, irand(95, 120));
        if (chance(35)) add(0, i + 2, 1, irand(80, 100));
        if (chance(25) && i + 3 < total16) add(0, i + 3, 1, irand(75, 95));
    }

    // hats: steady with occasional 32nd tickle
    for (int i = 0; i < total16; ++i)
    {
        if (i % 2 == 0 || chance(20)) add(2, i, 1, irand(70, 95)); // 8ths + random fills
        if (chance(12) && i + 1 < total16) add(2, i + 1, 1, irand(60, 85)); // light 16th echo
    }

    setDrumPattern(pat);
    notifyPatternChanged();
}


void BoomAudioProcessor::flipMelodic(int densityPct, int addPct, int removePct)
{
    auto pat = getMelodicPattern();

    // remove some
    for (int i = pat.size() - 1; i >= 0; --i)
        if (chance(removePct)) pat.remove(i);

    // add small grace/echo notes around existing
    for (int i = 0; i < pat.size(); ++i)
    {
        if (chance(addPct))
        {
            auto n = pat[i];
            const bool before = chance(50);
            const int  off16 = before ? -1 : 1;
            int start16 = (n.startTick / (PPQ / 4)) + off16;
            if (start16 >= 0)
                pat.add({ n.pitch + (chance(50) ? 0 : (chance(50) ? 1 : -1)),
                          toTick16(start16), toTick16(1), juce::jlimit(40,120, n.velocity - 10), n.channel });
        }
    }

    setMelodicPattern(pat);
    notifyPatternChanged();
}

void BoomAudioProcessor::flipDrums(int densityPct, int addPct, int removePct)
{
    auto pat = getDrumPattern();

    // remove
    for (int i = pat.size() - 1; i >= 0; --i)
        if (chance(removePct)) pat.remove(i);

    // add ghost notes (mostly hats / occasional kick)
    const int total16 = q16(4); // assume up to 4 bars; safe to overfill
    for (int i = 0; i < total16; ++i)
    {
        if (chance(addPct))
        {
            int row = chance(70) ? 2 : 0; // mostly hats
            int vel = row == 2 ? irand(50, 80) : irand(70, 95);
            pat.add({ 0, row, toTick16(i), toTick16(1), vel });
        }
    }

    setDrumPattern(pat);
    notifyPatternChanged();
}


void BoomAudioProcessor::generateRolls(const juce::String& style, int bars, int seed)
{
    std::mt19937 rng(seed == -1 ? std::random_device{}() : (unsigned)seed);

    int restPct = 10;
    const int dottedPct = getPct(apvts, "dottedDensity", 0);
    int       tripletPct = getPct(apvts, "tripletDensity", 0);
    const int swingPct = getPct(apvts, "swing", 0);
    if (style.equalsIgnoreCase("drill")) tripletPct = clampInt(tripletPct + 20, 0, 100);

    boom::drums::DrumStyleSpec spec = boom::drums::getSpec(style);
    boom::drums::DrumPattern pat;
    boom::drums::generate(spec, bars, restPct, dottedPct, tripletPct, swingPct, /*seed*/ -1, pat);

    auto cur = getDrumPattern();
    juce::Array<Note> out = cur;

    const int hatRow = 1; // adjust to your indices
    const int snareRow = 2;

    for (const auto& e : pat)
    {
        if (e.row == hatRow || e.row == snareRow)
        {
            Note dn;
            dn.row = e.row;
            dn.startTick = e.startTick;
            dn.lengthTicks = e.lenTicks;
            dn.velocity = juce::jlimit<int>(1, 127, e.vel);
            out.add(dn);
        }
    }
    setDrumPattern(out);
}

int BoomAudioProcessor::getTimeSigNumerator() const noexcept
{
    if (auto* v = apvts.getRawParameterValue("timeSigNum"))
        return juce::jlimit(1, 32, (int)std::lround(v->load()));
    return 4;
}

int BoomAudioProcessor::getBars() const
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("bars")))
    {
        // Choices are "4 Bars" (index 0) and "8 Bars" (index 1)
        if (p->getIndex() == 1)
            return 8;
    }
    return 4; // Default to 4 bars
}

double BoomAudioProcessor::getHostBpm() const noexcept
{
    const float lock = readParam(apvts, "bpmLock", 0.0f); // 0.0 or 1.0
    const float uiBpm = readParam(apvts, "bpm", 120.0f); // your BPM slider's value

    if (lock < 0.5f) // unlocked -> trust UI BPM
        return (double)uiBpm;

    // locked -> prefer host BPM, fall back to UI if host unknown
    auto bpm = lastHostBpm.load();
    return (bpm > 0.0) ? bpm : (double)uiBpm;
}

int BoomAudioProcessor::getTimeSigDenominator() const noexcept
{
    if (auto* v = apvts.getRawParameterValue("timeSigDen"))
        return juce::jlimit(1, 32, (int)std::lround(v->load()));
    return 4;
}


// --- Timer tick: refresh the BPM label (and anything else lightweight) ---
void BoomAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    // Store the real sample rate for capture/transcription and size the ring buffer.
    lastSampleRate = (sampleRate > 0.0 ? sampleRate : 44100.0);
    ensureCaptureCapacitySeconds(65.0);
    captureBuffer.clear(); // ~60s cap + a little margin
    captureWritePos = 0;
    captureLengthSamples = 0;
    isCapturing.store(false);
}

// IMPORTANT: This is where we append input audio to the capture ring buffer when recording.
// We keep the audio buffer silent because BOOM is a MIDI generator/transformer.
void BoomAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // --- 1) Keep our sample-rate fresh --------------------------------------
    lastSampleRate = getSampleRate() > 0.0 ? getSampleRate() : lastSampleRate;

    if (auto* ph = getPlayHead())
    {
        juce::AudioPlayHead::CurrentPositionInfo info;
        if (ph->getCurrentPosition(info))
        {
            if (info.bpm > 0.0)
                lastHostBpm = info.bpm;
        }
    }

    // --- 2) Poll host for BPM (JUCE 7/8 safe) -------------------------------
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (pos->getBpm().hasValue())
                lastHostBpm.store(*pos->getBpm());   // <-- make sure you have 'double lastHostBpm' in your class
            // If you want time sig too (only if you have members for them):
            // if (pos->getTimeSignature().has_value()) {
            //     auto ts = *pos->getTimeSignature();
            //     lastTimeSigNum = ts.numerator;
            //     lastTimeSigDen = ts.denominator;
            // }
        }
    }

    // --- 3) Capture recording (Rhythmimick / Beatbox) -----------------------
    // We record *mono* into captureBuffer by averaging L/R.
    const int numInCh = buffer.getNumChannels();
    const int numSmps = buffer.getNumSamples();

    // Compute input RMS (per block) so meters can move
    float accL = 0.0f, accR = 0.0f;

    if (numSmps > 0 && numInCh > 0)
    {
        const float* inL = buffer.getReadPointer(0);
        const float* inR = (numInCh > 1 ? buffer.getReadPointer(1) : nullptr);

        for (int i = 0; i < numSmps; ++i)
        {
            const float l = inL[i];
            const float r = inR ? inR[i] : l;
            accL += l * l;
            accR += r * r;
        }
    }

    // Smooth/store RMS for meters (editor polls these)
    const float n = (float)juce::jmax(1, numSmps);
    const float rmsL = std::sqrt(accL / n);
    const float rmsR = std::sqrt(accR / n);
    rmsInputL.store(rmsL);
    rmsInputR.store(rmsR);

    // If we’re actively capturing, push audio into our mono ring buffer
    if (isCapturing.load() && numSmps > 0 && numInCh > 0)
    {
        // Ensure capacity: at least ~65 seconds of mono at current SR
        const int want = (int)std::ceil(lastSampleRate * 65.0);
        if (captureBuffer.getNumChannels() != 1 || captureBuffer.getNumSamples() < want)
        {
            captureBuffer.setSize(1, want, false, true, true);
            captureWritePos = 0;
            captureLengthSamples = 0;
        }

        float* dst = captureBuffer.getWritePointer(0);
        const float* inL = buffer.getReadPointer(0);
        const float* inR = (numInCh > 1 ? buffer.getReadPointer(1) : nullptr);

        const int cap = captureBuffer.getNumSamples();

        // Write as a wrap-around ring
        for (int i = 0; i < numSmps; ++i)
        {
            const float mono = inR ? 0.5f * (inL[i] + inR[i]) : inL[i];
            dst[captureWritePos] = mono;

            captureWritePos = (captureWritePos + 1);
            if (captureWritePos >= cap) captureWritePos = 0;

            if (captureLengthSamples < cap)
                ++captureLengthSamples;
        }

        // Advance a lightweight "playhead" so the seekbar can move during record
        capturePlayheadSamples.store(capturePlayheadSamples.load() + numSmps);
    }
    else
    {
        // Not capturing -> keep playhead parked (editor shows 0..len)
        capturePlayheadSamples.store(0);
    }

    // --- 4) MIDI: leave 'midi' alone here -----------------------------------
    // Your generators run from UI callbacks and update internal patterns.
    // If you later want to emit MIDI every block (to drive external synths),
    // you’d translate your current pattern into 'midi' here based on the host time.
    // For now, do nothing so we don’t double-fire notes.

    // --- 5) If you are an instrument, you *may* want to output silence -------
    // If this plugin is a synth with no audio generation inside processBlock,
    // uncomment to avoid passing through input:
    // buffer.clear();
}

void BoomAudioProcessor::releaseResources()
{
    // Nothing heavy to free, but make sure capture is stopped and pointers reset.
    isCapturing.store(false);
    captureWritePos = 0;
    captureLengthSamples = 0;
}

void BoomAudioProcessor::ensureCaptureCapacitySeconds(double seconds)
{
    const int needed = (int)std::ceil(seconds * lastSampleRate);
    if (captureBuffer.getNumSamples() < needed)
        captureBuffer.setSize(1, needed, false, true, true);
}

void BoomAudioProcessor::appendCaptureFrom(const juce::AudioBuffer<float>& in)
{
    if (in.getNumSamples() <= 0) return;

    // mix all channels to mono temp
    juce::AudioBuffer<float> mono(1, in.getNumSamples());
    mono.clear();
    const int chans = in.getNumChannels();
    for (int ch = 0; ch < chans; ++ch)
        mono.addFrom(0, 0, in, ch, 0, in.getNumSamples(), 1.0f / juce::jmax(1, chans));

    // write into ring buffer (linear until full; stop if full ~ 60s)
    const int free = captureBuffer.getNumSamples() - captureWritePos;
    const int n = juce::jmin(free, mono.getNumSamples());
    if (n > 0)
        captureBuffer.copyFrom(0, captureWritePos, mono, 0, 0, n);

    captureWritePos += n;
    captureLengthSamples = juce::jmax(captureLengthSamples, captureWritePos);

    // once full, stop capturing (hard stop at ~ 60s)
    if (captureWritePos >= captureBuffer.getNumSamples())
        isCapturing.store(false);
}

BoomAudioProcessor::Pattern BoomAudioProcessor::transcribeAudioToDrums(const float* mono, int N, int bars, int bpm) const
{
    Pattern pat;
    if (mono == nullptr || N <= 0) return pat;

    const int fs = (int)lastSampleRate;
    const int hop = 512;
    const int win = 1024;
    const float preEmph = 0.97f;

    const int stepsPerBar = 16;
    const int totalSteps = juce::jmax(1, bars) * stepsPerBar;
    const double secPerBeat = 60.0 / juce::jlimit(40, 240, bpm);
    const double secPerStep = secPerBeat / 4.0; // 16th
    const int ticksPerStep = 24;

    auto bandEnergy = [&](int start, int end) -> std::vector<float>
    {
        std::vector<float> env;
        env.reserve(N / hop + 8);

        for (int i = 0; i + win <= N; i += hop)
        {
            float e = 0.f;
            for (int n = 0; n < win; ++n)
            {
                float x = mono[i + n] - preEmph * (n > 0 ? mono[i + n - 1] : 0.f);
                float w = 1.f;
                if (start >= 200 && end <= 2000) w = 0.7f;
                if (start >= 5000)               w = 0.5f;
                e += std::abs(x) * w;
            }
            e /= (float)win;
            env.push_back(e);
        }

        float mx = 1e-6f;
        for (auto v : env) mx = juce::jmax(mx, v);
        for (auto& v : env) v /= mx;

        return env;
    };

    auto low = bandEnergy(20, 200);
    auto mid = bandEnergy(200, 2000);
    auto high = bandEnergy(5000, 20000);

    auto detectPeaks = [&](const std::vector<float>& e, float thr, int minGapFrames)
    {
        std::vector<int> frames;
        int last = -minGapFrames;
        for (int i = 1; i + 1 < (int)e.size(); ++i)
        {
            if (e[i] > thr && e[i] > e[i - 1] && e[i] >= e[i + 1] && (i - last) >= minGapFrames)
            {
                frames.push_back(i);
                last = i;
            }
        }
        return frames;
    };

    auto kFrames = detectPeaks(low, 0.35f, (int)std::round(0.040 * fs / hop));
    auto sFrames = detectPeaks(mid, 0.30f, (int)std::round(0.050 * fs / hop));
    auto hFrames = detectPeaks(high, 0.28f, (int)std::round(0.030 * fs / hop));

    auto frameToTick = [&](int frame) -> int
    {
        double t = (double)(frame * hop) / fs;
        int step = (int)std::round(t / secPerStep);
        step = (step % totalSteps + totalSteps) % totalSteps;
        return step * ticksPerStep;
    };

    auto addHits = [&](const std::vector<int>& frames, int row, int vel)
    {
        for (auto f : frames)
            pat.add({ 0, row, frameToTick(f), 12, vel });
    };

    // rows: 0 kick, 1 snare, 2 hat
    addHits(kFrames, 0, 115);
    addHits(sFrames, 1, 108);
    addHits(hFrames, 2, 80);

    return pat;
}

void BoomAudioProcessor::aiAnalyzeCapturedToDrums(int bars, int bpm)
{
    if (captureLengthSamples <= 0) return;
    const int N = juce::jmin(captureLengthSamples, captureBuffer.getNumSamples());
    auto* mono = captureBuffer.getReadPointer(0);
    auto pat = transcribeAudioToDrums(mono, N, bars, bpm);
    setDrumPattern(pat);
}

void BoomAudioProcessor::aiStartCapture(CaptureSource src)
{
    // Stop any previous capture first
    aiStopCapture();

    // Remember what we’re capturing (Loopback or Microphone — your enum has only those two)
    currentCapture = src;

    // Make sure buffer exists for ~60 seconds (we’ll use 65s margin)
    lastSampleRate = getSampleRate() > 0.0 ? getSampleRate() : lastSampleRate;
    ensureCaptureCapacitySeconds(65.0);
    captureBuffer.clear();
    captureWritePos = 0;
    captureLengthSamples = 0;

    // Mark as capturing
    isCapturing.store(true, std::memory_order_release);

    if (auto* ed = getActiveEditor()) ed->repaint();
}

void BoomAudioProcessor::aiStopCapture()
{
    if (!isCapturing.load(std::memory_order_acquire))
        return;

    isCapturing.store(false, std::memory_order_release);
    if (auto* ed = getActiveEditor()) ed->repaint();
}

void BoomAudioProcessor::aiPreviewStart()
{
    if (captureLengthSamples <= 0) return;
    isPreviewing.store(true);
    previewReadPos = 0;
}

void BoomAudioProcessor::aiPreviewStop()
{
    isPreviewing.store(false);
}

double BoomAudioProcessor::getCaptureLengthSeconds() const noexcept
{
    return (lastSampleRate > 0.0)
        ? static_cast<double>(captureLengthSamples) / lastSampleRate
        : 0.0;
}

double BoomAudioProcessor::getCapturePositionSeconds() const noexcept
{
    return (lastSampleRate > 0.0)
        ? static_cast<double>(juce::jlimit(0, captureLengthSamples, previewReadPos)) / lastSampleRate
        : 0.0;
}

void BoomAudioProcessor::aiSeekToSeconds(double sec) noexcept
{
    if (lastSampleRate <= 0.0 || captureLengthSamples <= 0) return;
    const int target = (int)juce::jlimit(0.0, getCaptureLengthSeconds(), sec) * (int)lastSampleRate;
    previewReadPos = juce::jlimit(0, captureLengthSamples, target);
}


