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
#include <limits>


using AP = juce::AudioProcessorValueTreeState;

juce::AudioProcessorEditor* BoomAudioProcessor::createEditor()
{
    return new BoomAudioProcessorEditor(*this);
}

namespace boomfix
{
    inline float readParamRaw(juce::AudioProcessorValueTreeState& apvts,
        const char* id,
        float fallback)
    {
        if (auto* v = apvts.getRawParameterValue(id))
            return v->load();
        return fallback;
    }
}

namespace
{
    static const juce::StringArray kKeys { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    struct JuceStringLess {
        bool operator()(const juce::String& a, const juce::String& b) const noexcept
        {
            return a.compare(b) < 0;
        }
    };

    // full scale list you asked to keep (truncated here for space; keep your full one)
    static const std::map<juce::String, std::vector<int>, JuceStringLess> kScales = {
    {"Major",         {0, 2, 4, 5, 7, 9, 11}},
    {"Natural Minor", {0, 2, 3, 5, 7, 8, 10}},
    {"Harmonic Minor",{0, 2, 3, 5, 7, 8, 11}},
    {"Dorian",        {0, 2, 3, 5, 7, 9, 10}},
    {"Phrygian",      {0, 1, 3, 5, 7, 8, 10}},
    {"Lydian",        {0, 2, 4, 6, 7, 9, 11}},
    {"Mixolydian",    {0, 2, 4, 5, 7, 9, 10}},
    {"Aeolian",       {0, 2, 3, 5, 7, 8, 10}},
    {"Locrian",       {0, 1, 3, 5, 6, 8, 10}},
    {"Locrian ♮6",    {0, 1, 3, 5, 6, 9, 10}},
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
    {"Phyrgian ♮3",    {0, 1, 4, 5, 7, 8, 10}},
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
    {"Bebop Minor", {0, 2, 3, 5, 7, 8, 9, 10}},
    };


    static inline int wrap12(int v) { v %= 12; if (v < 0) v += 12; return v; }

    static inline int degreeToPitch(const juce::String& keyName,
        const juce::String& scaleName,
        int degree, int octave)
    {
        const int keyIdx = juce::jmax(0, kKeys.indexOf(keyName.trim().toUpperCase()));
        const auto it = kScales.find(scaleName.trim());
        const auto& pcs = (it != kScales.end()) ? it->second : kScales.at("Chromatic");

        const int N = (int)pcs.size();
        const int di = ((degree % N) + N) % N;
        const int pc = pcs[di];

        const int pitch = octave * 12 + wrap12(keyIdx + pc);
        return juce::jlimit(0, 127, pitch);
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

namespace
{
    // 24 ticks per 1/16 step to align with your grid
    constexpr int kTicksPerStep = 24;

    struct HarmPlan
    {
        // 0 = root-only; 1 = chord-roots per half/bar with 5th/other spices
        int mode = 0;
    };

    // Simple note-placing helper that avoids overlapping starts
    template <typename MelPat>
    void placeNoteUnique(MelPat& mp, std::map<int, bool>& noteGrid,
        int startTick, int lenTick, int pitch, int vel)
    {
        if (noteGrid.count(startTick)) return;
        mp.add({ pitch, startTick, lenTick, juce::jlimit(1,127,vel), 1 });
        noteGrid[startTick] = true;
    }

    inline int jclamp(int v, int lo, int hi) { return (v < lo ? lo : (v > hi ? hi : v)); }

    // Build a list of onset steps for N bars given stepsPerBar and a density feel
    // 'cell' here is a 1/16 step in 4/4; we generalize via stepsPerBar
    static void makeOnsets(int stepsPerBar, int bars, juce::Random& rng,
        int gridChoice /*0=8ths,1=quarters,2=8thTs,3=quarterTs,4=16ths (rare)*/,
        bool longNotes, std::vector<int>& outSteps)
    {
        outSteps.clear();

        const bool isTrip = (gridChoice == 2 || gridChoice == 3);
        const bool is16 = (gridChoice == 4);

        const int totalSteps = stepsPerBar * bars;

        // Base “kick-like” grids
        auto pushIf = [&](int step, int prob) {
            if (step >= 0 && step < totalSteps && rng.nextInt({ 100 }) < prob)
                outSteps.push_back(step);
        };

        // Seed some anchor hits like a kick pattern: 1, (and of 2), 3, (4)
        for (int b = 0; b < bars; ++b)
        {
            const int base = b * stepsPerBar;

            // Beat 1
            pushIf(base + 0, 100);

            // Beat 3
            pushIf(base + (stepsPerBar / 2), 85);

            // "and" of 2 (midpoint between beat2 and beat3)
            pushIf(base + (stepsPerBar / 4) * 3, isTrip ? 55 : 70);

            // Optional beat 4
            pushIf(base + (stepsPerBar / 4) * 3 + (stepsPerBar / 4), is16 ? 60 : 45);

            // Sprinkle more based on mode
            if (gridChoice == 0) // 8th-heavy
            {
                // Add some 8th off-beats
                pushIf(base + stepsPerBar / 4, 65);
                pushIf(base + stepsPerBar / 4 * 2 + stepsPerBar / 8, 55); // and of 3
            }
            else if (gridChoice == 1) // quarter-heavy (sparser)
            {
                // Maybe just reinforce beat 2/4
                pushIf(base + stepsPerBar / 4, 50);
                pushIf(base + stepsPerBar / 4 * 3, 50);
            }
            else if (gridChoice == 2) // 8th-triplet feel
            {
                // Triplet grid: split each beat into 3 — emulate by nudging off the straight grid
                // We'll add extra hits slightly “early/late” around 8ths
                pushIf(base + stepsPerBar / 6, 60);
                pushIf(base + stepsPerBar / 6 * 3, 55);
                pushIf(base + stepsPerBar / 6 * 5, 45);
            }
            else if (gridChoice == 3) // quarter-triplet (rare)
            {
                pushIf(base + stepsPerBar / 6, 40);
                pushIf(base + stepsPerBar / 6 * 3, 40);
                pushIf(base + stepsPerBar / 6 * 5, 40);
            }
            else // 16th-heavy (very rare)
            {
                // sprinkle a few 16ths around beats 1 & 3
                pushIf(base + 1, 50);
                pushIf(base + 2, 35);
                pushIf(base + (stepsPerBar / 2) + 1, 50);
                pushIf(base + (stepsPerBar / 2) + 2, 35);
            }
        }

        // De-dup & sort
        std::sort(outSteps.begin(), outSteps.end());
        outSteps.erase(std::unique(outSteps.begin(), outSteps.end()), outSteps.end());

        // If “short notes with space”, thin the onsets slightly
        if (!longNotes)
        {
            std::vector<int> thin;
            thin.reserve(outSteps.size());
            for (int s : outSteps)
                if (rng.nextInt({ 100 }) < 85) thin.push_back(s);
            outSteps.swap(thin);
        }
    }

    // Choose which harmonic strategy to use
    static HarmPlan chooseHarmPlan(juce::Random& rng)
    {
        HarmPlan hp;
        // 0: root-only (90%), 1: chord-root shifts (10%)
        hp.mode = (rng.nextInt({ 100 }) < 90 ? 0 : 1);
        return hp;
    }



    // Quick scale tables (same as you’ve used elsewhere)

    struct KeyScale {
        int rootIndex = 0;                       // 0..11 (C..B)
        const std::vector<int>* pcs = nullptr;   // pointer to pitch-class vector
    };

    inline KeyScale readKeyScale(juce::AudioProcessorValueTreeState& apvts)
    {
        KeyScale ks;
        juce::String keyName = "C";
        juce::String scaleName = "Major";

        if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("key")))
            keyName = p->getCurrentChoiceName();

        if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("scale")))
            scaleName = p->getCurrentChoiceName();

        ks.rootIndex = juce::jmax(0, kKeys.indexOf(keyName.trim().toUpperCase()));

        auto it = kScales.find(scaleName.trim());
        if (it == kScales.end())
            it = kScales.find("Chromatic");

        ks.pcs = &it->second;
        return ks;
    }

    // Convert (degree index in scale, absolute octave) -> MIDI note
    inline int degreeToPitch(int degree, int octave, const KeyScale& ks)
    {
        const auto& scale = *ks.pcs;
        const int safeDeg = (int)((degree % (int)scale.size() + (int)scale.size()) % (int)scale.size());
        const int pc = scale[safeDeg];                    // pitch class from scale
        const int midi = octave * 12 + wrap12(ks.rootIndex + pc);
        return juce::jlimit(0, 127, midi);
    }
}


namespace
{
    // Your grid constants
    constexpr int kTicksPerQuarter = 96;
    constexpr int kTicksPer16 = kTicksPerQuarter / 4; // 24 // you’re using 24 ticks per 1/16 “step”

    // Time-signature → steps per bar (16ths for /4, 8ths for /8, etc.)
    inline int stepsPerBarFor(int num, int den)
    {
        if (den == 4) return num * 4; // 4/4 -> 16 steps
        if (den == 8) return num * 2; // 6/8 -> 12 steps
        if (den == 16) return num;
        return num * 4; // default
    }

    // Prevent stacked notes: keep a set of occupied tick “buckets”
    struct TickGuard
    {
        // using buckets to allow a spacing tolerance (e.g. within 12 ticks)
        std::unordered_set<int> buckets; // bucket = tick / bucketSize
        int bucketSize = 12; // default ~half a 16th

        bool tryLock(int tick)
        {
            const int b = tick / bucketSize;
            if (buckets.find(b) != buckets.end()) return false;
            buckets.insert(b);
            return true;
        }
    };

    // Add note if the (bucketed) tick is free. Returns true if placed.
    template <typename PatternT>
    inline bool placeNoteUnique(PatternT& mp,
        TickGuard& guard,
        int startTick,
        int lengthTicks,
        int pitch,
        int velocity,
        int channel)
    {
        if (!guard.tryLock(startTick))
            return false;

        // Event field order you used later:
        // { pitch, startTick, lengthTicks, velocity, channel }
        mp.add({ pitch,
                 startTick,
                 juce::jmax(6, lengthTicks),
                 juce::jlimit(1,127,velocity),
                 juce::jlimit(1,16,channel) });

        return true;
    }
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

// ----- 808 Generator (bars: 1/2/4/8; density 0..100; honors style/key/scale/TS) -----
std::pair<int, int> BoomAudioProcessor::getMelodicPitchBounds() const
{
    const auto& pat = melodicPattern; // whatever you store your Mel notes in
    if (pat.isEmpty())
        return { 36, 84 }; // sane default: C2..C6

    int lo = 127, hi = 0;
    for (const auto& n : pat)
    {
        lo = juce::jmin(lo, n.pitch);
        hi = juce::jmax(hi, n.pitch);
    }
    // pad a bit
    lo = juce::jlimit(0, 127, lo - 2);
    hi = juce::jlimit(0, 127, hi + 2);
    return { lo, hi };
}

// ============================================================
// Bass Generator – rhythm-first, style-weighted, variety-safe.
// ============================================================

namespace
{
    // 96 ticks/quarter => 24 per 1/16 (you already use this grid elsewhere)

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

// --- generate808: bars, octave offset, density %, triplets, dotted, seed ---
void BoomAudioProcessor::generate808(int bars,
    int octave,
    int densityPct,
    bool allowTriplets,
    bool allowDotted,
    int seed)
{
    // Clamp inputs
    bars = juce::jlimit(1, 8, bars);
    densityPct = juce::jlimit(0, 100, densityPct);

    // --- read key/scale from APVTS ---
    juce::String keyName = "C";
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("key")))
        keyName = p->getCurrentChoiceName();

    juce::String scaleName = "Natural Minor";
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("scale")))
        scaleName = p->getCurrentChoiceName();

    // --- time signature -> steps per bar (grid family) ---
    auto ts = apvts.state.getProperty("timeSig").toString();
    if (ts.isEmpty()) ts = "4/4";
    auto tsParts = juce::StringArray::fromTokens(ts, "/", "");
    const int tsNum = tsParts.size() > 0 ? tsParts[0].getIntValue() : 4;
    const int tsDen = tsParts.size() > 1 ? tsParts[1].getIntValue() : 4;

    auto stepsPerBarFor = [](int n, int d)
    {
        if (d == 4) return n * 4; // 16ths = 4 per beat
        if (d == 8) return n * 2; // treat 8ths as base
        if (d == 16) return n; // 16th note as base unit
        return n * 4;
    };
    const int stepsPerBar = stepsPerBarFor(tsNum, tsDen);
    const int totalSteps = stepsPerBar * bars;

    // --- resolve key/scale tables (kKeys/kScales/wrap12 must exist once at file top) ---
    const int keyIdx = juce::jmax(0, kKeys.indexOf(keyName.trim().toUpperCase()));
    const auto it = kScales.find(scaleName.trim());
    const auto& pcs = (it != kScales.end()) ? it->second : kScales.at("Chromatic");

    auto degreeToPitch = [&](int degree, int oct) -> int
    {
        const int pcIndex = (degree % (int)pcs.size() + (int)pcs.size()) % (int)pcs.size();
        const int pc = pcs[pcIndex];
        const int pitch = oct * 12 + wrap12(keyIdx + pc);
        return juce::jlimit(0, 127, pitch);
    };

    // --- randomness (reproducible if seed >= 0) ---
    int seedUsed = seed;
    if (seedUsed < 0)
    {
        const auto now32 = (int)juce::Time::getMillisecondCounter();
        const auto ticks = (int)(juce::Time::getHighResolutionTicks() & 0x7fffffff);
        seedUsed = now32 ^ ticks;
    }
    juce::Random rng(seedUsed);
    auto pct = [&](int p)->bool { return rng.nextInt({ 100 }) < juce::jlimit(0, 100, p); };

    // --- rhythm family choice (8th, quarter, 8th-triplet, rare quarter-triplet, very rare 16th) ---
    int gridChoice;
    {
        int r = rng.nextInt({ 100 });
        if (r < 40) gridChoice = 0; // 8th-heavy
        else if (r < 70) gridChoice = 1; // quarter-heavy
        else if (r < 90) gridChoice = 2; // 8th-triplet
        else if (r < 97) gridChoice = 3; // quarter-triplet (rare)
        else gridChoice = 4; // 16th-heavy (very rare)
        if (!allowTriplets && (gridChoice == 2 || gridChoice == 3))
            gridChoice = 0;
    }

    const bool longNotes = pct(50); // long vs short 808s
    const bool complex = pct(45); // rolls/risks

    // --- build onset steps like kick patterns ---
    std::vector<int> onsets;
    onsets.reserve(64);

    auto pushUnique = [&](int s)
    {
        if (s >= 0 && s < totalSteps && std::find(onsets.begin(), onsets.end(), s) == onsets.end())
            onsets.push_back(s);
    };

    auto addBarOnsets = [&](int barIdx)
    {
        const int base = barIdx * stepsPerBar;
        switch (gridChoice)
        {
        case 0: // 8ths
        {
            for (int b = 0; b < tsNum; ++b)
            {
                const int step0 = base + b * (stepsPerBar / tsNum);
                // primary kick-ish at beat, optional “&”
                pushUnique(step0);
                if (pct(55)) pushUnique(step0 + (stepsPerBar / (tsNum * 2))); // the “&”
            }
            break;
        }
        case 1: // quarters
        {
            for (int b = 0; b < tsNum; ++b)
                pushUnique(base + b * (stepsPerBar / tsNum));
            // occasional extra pick-up
            if (pct(35))
                pushUnique(base + (stepsPerBar / tsNum) - 1);
            break;
        }
        case 2: // 8th-triplet feel (approx: push two hits between beats)
        {
            for (int b = 0; b < tsNum; ++b)
            {
                const int beat = base + b * (stepsPerBar / tsNum);
                pushUnique(beat);
                if (allowTriplets)
                {
                    pushUnique(beat + juce::jmax(1, (stepsPerBar / (tsNum * 3))));
                    pushUnique(beat + juce::jmax(2, (2 * stepsPerBar / (tsNum * 3))));
                }
            }
            break;
        }
        case 3: // quarter-triplet (rare)
        {
            for (int b = 0; b < tsNum - 1; ++b)
            {
                const int beat = base + b * (stepsPerBar / tsNum);
                if (allowTriplets)
                {
                    pushUnique(beat);
                    pushUnique(beat + juce::jmax(1, (stepsPerBar / (tsNum * 3))));
                    pushUnique(beat + juce::jmax(2, (2 * stepsPerBar / (tsNum * 3))));
                }
                else
                    pushUnique(beat);
            }
            break;
        }
        default: // 16ths-heavy
        {
            for (int s = 0; s < stepsPerBar; s += 1) // dense
                if (pct(densityPct)) pushUnique(base + s);
            break;
        }
        }
    };

    for (int b = 0; b < bars; ++b) addBarOnsets(b);
    std::sort(onsets.begin(), onsets.end());

    // --- choose harmonic plan: mostly root vs chord roots with some 5ths/octaves ---
    struct HarmPlan { bool chordRoots; int specialChance; }; // % prob to deviate
    HarmPlan hp;
    if (pct(90))
        hp = { false, 10 }; // mostly root
    else
        hp = { true, 35 }; // move around (roots/5ths/others)

    // --- construct melodic pattern ---
    auto mp = getMelodicPattern();
    mp.clear();

    const int tps = 24; // ticks per 1/16 step
    const int baseOct = juce::jlimit(0, 10, 2 + octave);
    int currentDeg = 0; // degree index into pcs
    int currentOct = baseOct;

    auto addNote = [&](int step, int lenSteps, int vel)
    {
        const int startTick = step * tps;
        const int lenTick = juce::jmax(6, lenSteps * tps);
        const int pitch = degreeToPitch(currentDeg, currentOct);
        mp.add({ pitch, startTick, lenTick, juce::jlimit(1,127,vel), 1 });
    };

    // fill from onsets
    for (size_t i = 0; i < onsets.size(); ++i)
    {
        const int s = onsets[i];
        // pick note length
        int lenSteps = longNotes ? 2 : 1;
        if (allowDotted && pct(20)) lenSteps += lenSteps / 2; // dotted *1.5

        // avoid overrun next onset
        if (i + 1 < onsets.size())
            lenSteps = juce::jmin(lenSteps, juce::jmax(1, onsets[i + 1] - s));

        // harmonic choice
        if (!hp.chordRoots)
        {
            // 90% root, occasional 5th/octave
            const int r = rng.nextInt({ 100 });
            if (r < 80) currentDeg = 0; // root
            else if (r < 92) currentDeg = 4; // fifth-ish
            else currentDeg = 7; // octave-ish (by scale index)
        }
        else
        {
            // walk chord-ish degrees, sometimes 5th/neighbor
            if (pct(50)) currentDeg = 0;
            else if (pct(65)) currentDeg = 4;
            else if (pct(hp.specialChance))
                currentDeg += (rng.nextBool() ? +1 : -1); // neighbor wiggle
        }

        // complex? add small rolls
        if (complex && pct(35))
        {
            int t = s * tps;
            const int endT = t + juce::jmax(6, lenSteps * tps);
            const int sub = allowTriplets ? 8 : 12; // faster if triplets allowed
            while (t < endT)
            {
                const int subTick = juce::jmax(3, juce::jmin(sub, endT - t));
                const int pitch = degreeToPitch(currentDeg, currentOct);
                mp.add({ pitch, t, subTick, 96 + rng.nextInt({20}), 1 });
                if (pct(30)) currentDeg += (rng.nextBool() ? +1 : -1);
                t += subTick;
            }
        }
        else
        {
            addNote(s, lenSteps, 100);
        }

        // small octave hops sometimes
        if (pct(10)) currentOct = juce::jlimit(0, 10, currentOct + (rng.nextBool() ? +1 : -1));
    }

    setMelodicPattern(mp);

    if (auto* ed = getActiveEditor())
        ed->repaint();
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
    // ---- Get params from APVTS ----
    juce::String keyName = "C";
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("key")))
        keyName = p->getCurrentChoiceName();
    juce::String scaleName = "Major";
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("scale")))
        scaleName = p->getCurrentChoiceName();
    int densityPercent = 100 - restPct;
    bool allowTriplets = readParam(apvts, "useTriplets", 0.0f) > 0.5f;
    bool allowDotted = readParam(apvts, "useDotted", 0.0f) > 0.5f;
    const juce::String& style = styleName;

    // ---- Config & helpers ----
    bars = juce::jlimit(1, 8, bars);
    densityPercent = juce::jlimit(0, 100, densityPercent);

    // time signature -> steps per bar
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
    const int tps = kTicksPerStep; // 24
    const int totalSteps = stepsPerBar * bars;                     // ticks per 1/16 step (your grid)
    const int ppq = 96;                         // for export (not used here)

    // ---- Scale tables ----
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
    {"Locrian ♮6",    {0, 1, 3, 5, 6, 9, 10}},
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
    {"Phyrgian ♮3",    {0, 1, 4, 5, 7, 8, 10}},
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
    {"Bebop Minor", {0, 2, 3, 5, 7, 8, 9, 10}},
    };
    static const juce::StringArray kKeys = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

    auto keyIndex = juce::jmax(0, kKeys.indexOf(keyName.trim().toUpperCase()));
    const auto itScale = kScales.find(scaleName.trim());
    const auto& scalePCs = (itScale != kScales.end()) ? itScale->second : kScales.at("Chromatic");

    auto wrap12 = [](int v) { v %= 12; if (v < 0) v += 12; return v; };

    auto degreeToPitch = [&](int degree, int oct)->int
    {
        // degree is index in scalePCs (wrap), root = keyIndex
        const int pc = scalePCs[(degree % (int)scalePCs.size() + (int)scalePCs.size()) % (int)scalePCs.size()];
        return juce::jlimit(0, 127, oct * 12 + wrap12(keyIndex + pc));
    };

    // ---- Randomness (never same twice) ----
    const auto now32 = juce::Time::getMillisecondCounter();
    const auto ticks64 = (std::uint64_t)juce::Time::getHighResolutionTicks();
    const auto nonce = genNonce_.fetch_add(1, std::memory_order_relaxed) + 1;

    const std::uint64_t mix = (std::uint64_t)now32
        ^ (ticks64)
        ^ (std::uint64_t)nonce;

    const int rng_seed = (seed == -1) ? (int)(mix & 0x7fffffff) : seed;
    juce::Random rng(rng_seed);
    auto pct = [&](int prob)->bool { return rng.nextInt({ 100 }) < juce::jlimit(0, 100, prob); };

    // ---- Clear melodic pattern ----
    auto mp = getMelodicPattern();
    mp.clear();

    TickGuard guard;
    guard.bucketSize = 12; // ~half a 16th; widen if you still see clumps (try 16)
    const int channelBass = 1;

    // Base octave and note length policy per style
    int baseOct = 3 + octave; // C3-ish base for 808
    int sustainStepsDefault = (tsDen == 8 ? 2 : 1);

    if (style.equalsIgnoreCase("trap") || style.equalsIgnoreCase("wxstie") || style.equalsIgnoreCase("drill"))
    {
        baseOct = 2 + octave;
        sustainStepsDefault = 1;
    }

    // Density → how often we place notes per step
    auto placeProbability = [&](int step)->int
    {
        int base = densityPercent;
        if (style.equalsIgnoreCase("trap") || style.equalsIgnoreCase("drill"))
            if ((step % 2) == 1) base = juce::jmin(100, base + 8);
        return base;
    };

    // Choose rhythmic subdivision for a “burst”
    auto chooseSubTick = [&]()
    {
        int pool[] = { 24, 12, 8, 6, 4 };
        int idx = rng.nextInt({ (int)std::size(pool) });
        int sub = pool[idx];
        if (!allowTriplets && sub == 8) sub = 12;
        return sub;
    };

    // Decide dotted: multiply steps by 1.5
    auto dottedLen = [&](int steps)->int
    {
        if (!allowDotted) return steps;
        return steps + steps / 2;
    };

    // Melodic motion choices by style
    auto chooseDegreeDelta = [&]()
    {
        if (style.equalsIgnoreCase("trap"))
        {
            int r = rng.nextInt({ 100 });
            if (r < 40) return 0;
            if (r < 65) return 4;
            if (r < 80) return -3;
            if (r < 90) return 7;
            return (rng.nextBool() ? +1 : -1);
        }
        else if (style.equalsIgnoreCase("drill"))
        {
            int r = rng.nextInt({ 100 });
            if (r < 35) return 0;
            if (r < 60) return 4;
            if (r < 75) return -2;
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
            int r = rng.nextInt({ 100 });
            if (r < 50) return 0;
            if (r < 75) return 4;
            return (rng.nextBool() ? +1 : -1);
        }
    };

    int currentDegree = 0;
    int currentOct = baseOct;

    auto addNote = [&](int step, int lenSteps, int vel)
    {
        const int startTick = step * tps;
        const int lenTick = lenSteps * tps;
        const int v = 96 + rng.nextInt({ 20 });
        const int pitch = degreeToPitch(currentDegree, currentOct);

        if (!placeNoteUnique(mp, guard, startTick, lenTick, pitch, v, channelBass))
            placeNoteUnique(mp, guard, startTick + 12, lenTick, pitch, v, channelBass);
    };

    for (int step = 0; step < totalSteps; )
    {
        if (!pct(placeProbability(step))) { ++step; continue; }

        const bool doBurst = (style.equalsIgnoreCase("trap") || style.equalsIgnoreCase("drill")) ? pct(55) : pct(25);

        if (doBurst)
        {
            int sub = chooseSubTick();
            int durSteps = juce::jlimit(1, 4, 1 + rng.nextInt({ 3 }));
            int lenTickTotal = durSteps * tps;
            int t = step * tps;
            int endT = t + lenTickTotal;

            int localDeg = currentDegree;
            while (t < endT)
            {
                const int subTick = juce::jmax(3, juce::jmin(sub, endT - t));
                const int v = 90 + rng.nextInt({ 25 });
                const int pitch = degreeToPitch(localDeg, currentOct);

                if (!placeNoteUnique(mp, guard, t, subTick, pitch, v, channelBass))
                {
                    t += subTick;
                    continue;
                }

                if (pct(35)) localDeg += (rng.nextBool() ? +1 : -1);
                t += subTick;
            }
            step += durSteps;
            if (pct(20)) currentOct = juce::jlimit(1, 6, currentOct + (rng.nextBool() ? +1 : -1));
        }
        else
        {
            int lenSteps = sustainStepsDefault + rng.nextInt({ 2 });
            if (pct(20)) lenSteps = dottedLen(lenSteps);

            addNote(step, lenSteps, 96 + rng.nextInt({ 20 }));
            step += lenSteps;

            currentDegree += chooseDegreeDelta();

            if (pct(10)) currentOct = juce::jlimit(1, 6, currentOct + (rng.nextBool() ? +1 : -1));
        }
    }

    setMelodicPattern(mp);
    if (auto* ed = getActiveEditor()) ed->repaint();
}



BoomAudioProcessor::BoomAudioProcessor()
    : juce::AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
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

    TickGuard guard;
    guard.bucketSize = 8; // ~1/3 of a 16th; tight but prevents exact pile-ups
    const int channel808 = 1; // keep whatever channel you want

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

void BoomAudioProcessor::ai_beginRhRecord() { ai_rh_rec_.store(true); }
void BoomAudioProcessor::ai_endRhRecord() { ai_rh_rec_.store(false); }
bool BoomAudioProcessor::ai_isRhRecording() const noexcept { return ai_rh_rec_.load(); }

void BoomAudioProcessor::ai_beginBxRecord() { ai_bx_rec_.store(true); }
void BoomAudioProcessor::ai_endBxRecord() { ai_bx_rec_.store(false); }
bool BoomAudioProcessor::ai_isBxRecording() const noexcept { return ai_bx_rec_.load(); }

void BoomAudioProcessor::ai_stopAllAIPlayback() { /* no-op for now */ }

// if you don’t like this helper you can remove it; UI can just read the atomics directly
void BoomAudioProcessor::ai_tickAIMeters() { /* no-op; levels updated in processBlock */ }


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
            pat.add({ 0, row, i, 1, vel, 1, });
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
    ai_rh_buf_.setSize(1, 48000 * 30); // 30s mono at 48k; change if you want
    ai_bx_buf_.setSize(1, 48000 * 30);
    ai_rh_write_ = 0;
    ai_bx_write_ = 0;
    
    // Store the real sample rate for capture/transcription and size the ring buffer.
    lastSampleRate = (sampleRate > 0.0 ? sampleRate : 44100.0);
    ensureCaptureCapacitySeconds(65.0);
    captureBuffer.clear(); // ~60s cap + a little margin
    captureWritePos = 0;
    captureLengthSamples = 0;
}

// IMPORTANT: This is where we append input audio to the capture ring buffer when recording.
// We keep the audio buffer silent because BOOM is a MIDI generator/transformer.
void BoomAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // --- 1) Keep our sample-rate fresh --------------------------------------
    lastSampleRate = getSampleRate() > 0.0 ? getSampleRate() : lastSampleRate;

    // --- 2) Poll host for BPM (JUCE 7/8 safe) -------------------------------
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (pos->getBpm().hasValue())
                lastHostBpm.store(*pos->getBpm());   // <--make sure you have 'double lastHostBpm' in your class
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
    if (aiIsCapturing())
    {
        appendCaptureFrom(buffer);
    }
    const int numInCh = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    const float* inL = buffer.getReadPointer(0);
    const float* inR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : inL;

    // live input RMS for meters (cheap)
    float lRms = 0.0f, rRms = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        lRms += inL[i] * inL[i];
        rRms += inR[i] * inR[i];
    }
    lRms = std::sqrt(lRms / (float)juce::jmax(1, numSamples));
    rRms = std::sqrt(rRms / (float)juce::jmax(1, numSamples));
    ai_rh_inL_.store(lRms);
    ai_rh_inR_.store(rRms);
    ai_bx_inL_.store(lRms);
    ai_bx_inR_.store(rRms);

    // record if armed (mono mix)
    auto mixMono = [&](float l, float r) { return 0.5f * (l + r); };

    if (ai_rh_rec_.load())
    {
        float* dst = ai_rh_buf_.getWritePointer(0);
        const int N = ai_rh_buf_.getNumSamples();
        for (int i = 0; i < numSamples; ++i)
        {
            dst[ai_rh_write_] = mixMono(inL[i], inR[i]);
            ai_rh_write_ = (ai_rh_write_ + 1) % N;
        }
    }

    if (ai_bx_rec_.load())
    {
        float* dst = ai_bx_buf_.getWritePointer(0);
        const int N = ai_bx_buf_.getNumSamples();
        for (int i = 0; i < numSamples; ++i)
        {
            dst[ai_bx_write_] = mixMono(inL[i], inR[i]);
            ai_bx_write_ = (ai_bx_write_ + 1) % N;
        }
    }
}

void BoomAudioProcessor::releaseResources()
{
    // Nothing heavy to free, but make sure capture is stopped and pointers reset.
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
        aiStopCapture(currentCapture);
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
    aiStopCapture(currentCapture);

    // Remember what we’re capturing (Loopback or Microphone — your enum has only those two)
    currentCapture = src;

    // Make sure buffer exists for ~60 seconds (we’ll use 65s margin)
    lastSampleRate = getSampleRate() > 0.0 ? getSampleRate() : lastSampleRate;
    ensureCaptureCapacitySeconds(65.0);
    captureBuffer.clear();
    captureWritePos = 0;
    captureLengthSamples = 0;

    // Mark as capturing
    if (src == CaptureSource::Loopback)
    {
		recRh_ = true;
	}
	else
    {
		recBx_ = true;
	}

    if (auto* ed = getActiveEditor()) ed->repaint();
}

void BoomAudioProcessor::aiStopCapture(CaptureSource src)
{
    if (src == CaptureSource::Loopback)
    {
        recRh_ = false;
    }
    else
    {
        recBx_ = false;
    }
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

