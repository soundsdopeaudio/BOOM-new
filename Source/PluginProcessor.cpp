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
#include <iterator>


using AP = juce::AudioProcessorValueTreeState;

juce::AudioProcessorEditor* BoomAudioProcessor::createEditor()
{
    return new BoomAudioProcessorEditor(*this);
}

namespace {
    // Forward-declare the PPQ/tick constants and helpers used earlier in the file.
    // The real definitions remain later in this TU; these declarations let earlier functions compile.
    constexpr int kPPQ = 96;
    constexpr int kT16 = kPPQ / 4; // 24
    constexpr int kT8 = kPPQ / 2; // 48
    constexpr int kT4 = kPPQ;     // 96
    constexpr int kT8T = kPPQ / 3; // 32 (8th-triplet)
    constexpr int kT16T = kT8 / 3; // 16 (16th-triplet)
    constexpr int kT32 = kT16 / 2; // 12

    inline int stepsPerBarFromTS(int num, int den);
    inline void addNote(juce::MidiMessageSequence& seq, int startTick, int lenTicks,
        int midiNote, int vel, int channel);
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



static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using AP = juce::AudioProcessorValueTreeState;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterBool>("bpmLock", "BPM Lock", true));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("engine", "Engine", boom::engineChoices(), (int)boom::Engine::Drums));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("timeSig", "Time Signature", boom::timeSigChoices(), 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("bars", "Bars", boom::barsChoices(), 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("humanizeTiming", "Humanize Timing", juce::NormalisableRange<float>(0.f, 100.f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("humanizeVelocity", "Humanize Velocity", juce::NormalisableRange<float>(0.f, 100.f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("swing", "Swing", juce::NormalisableRange<float>(0.f, 100.f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("useTriplets", "Triplets", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("tripletDensity", "Triplet Density", juce::NormalisableRange<float>(0.f, 100.f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("useDotted", "Dotted Notes", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("dottedDensity", "Dotted Density", juce::NormalisableRange<float>(0.f, 100.f), 0.f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("key", "Key", boom::keyChoices(), 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("scale", "Scale", boom::scaleChoices(), 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("octave", "Octave", juce::StringArray("-2", "-1", "0", "+1", "+2"), 2));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("restDensity808", "Rest Density 808", juce::NormalisableRange<float>(0.f, 100.f), 10.f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("bassStyle", "Bass Style", boom::styleChoices(), 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("drumStyle", "Drum Style", boom::styleChoices(), 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("restDensityDrums", "Rest Density Drums", juce::NormalisableRange<float>(0.f, 100.f), 5.f));
    params.push_back(std::make_unique<juce::AudioParameterInt>("seed", "Seed", 0, 1000000, 0));

    // Move the unique_ptrs into the ParameterLayout using move-iterators
    return { std::make_move_iterator(params.begin()), std::make_move_iterator(params.end()) };
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

    struct EightOhEightSpec
    {
        int weight16[16];      // kick 16th weight map
        float splitTo32Prob;   // chance to add trap-style 1/32 roll
        float baseDensity;     // baseline density before rest slider
        float syncBias;        // pushes notes off the grid (0=no bias)
    };

    static EightOhEightSpec get808Spec(const juce::String& styleLower)
    {
        EightOhEightSpec s = {
            // default = trap / modern hip hop balance
            { 10,3,6,3,  9,3,7,3,  10,3,6,3,  9,3,7,3 },
            0.25f,   // some rolls
            0.65f,
            0.25f    // pushes offbeats
        };

        if (styleLower == "drill")
        {
            EightOhEightSpec d = {
                { 7,3,9,2,  6,3,9,2,  7,3,9,2,  6,3,9,2 },
                0.40f,   // more 32nd rolls
                0.55f,
                0.35f    // heavy offbeat bias
            };
            return d;
        }

        if (styleLower == "hip hop" || styleLower == "hiphop")
        {
            EightOhEightSpec h = {
                { 10,2,4,2,  8,2,5,2,  10,2,4,2,  8,2,5,2 },
                0.12f,
                0.50f,
                0.10f
            };
            return h;
        }

        if (styleLower == "trap")
        {
            EightOhEightSpec t = {
                { 11,3,7,3,  10,3,7,3,  11,3,7,3,  10,3,7,3 },
                0.30f,
                0.70f,
                0.30f
            };
            return t;
        }

        return s;
    }

    static inline float urand01(juce::Random& rng) { return rng.nextFloat(); }
}


void BoomAudioProcessor::aiStyleBlendDrums(const juce::String& styleA,
    const juce::String& styleB,
    int bars,
    float wA,
    float wB)
{
    // --- Normalize weights to 0..1 and nonzero sum ---
    wA = std::max(0.0f, wA);
    wB = std::max(0.0f, wB);
    float sum = wA + wB;
    if (sum <= 0.0001f) { wA = 0.5f; wB = 0.5f; sum = 1.0f; }
    wA /= sum; wB /= sum;

    // --- Current global feel (identical param IDs you already use elsewhere) ---
    const int restPct = getPct(apvts, "restDensity", 0);
    const int dottedPct = getPct(apvts, "dottedDensity", 0);
    const int tripletPct = getPct(apvts, "tripletDensity", 0);
    const int swingPct = getPct(apvts, "swing", 0);

    // --- Validate style names against your style DB ---
    const auto all = boom::drums::styleNames();
    if (!all.contains(styleA) || !all.contains(styleB))
        return; // silently bail if either is unknown

    // --- Generate both style patterns with the same bars/feel ---
    boom::drums::DrumPattern patA, patB;
    boom::drums::DrumStyleSpec specA = boom::drums::getSpec(styleA);
    boom::drums::DrumStyleSpec specB = boom::drums::getSpec(styleB);
    boom::drums::generate(specA, bars, restPct, dottedPct, tripletPct, swingPct, /*seed*/ -1, patA);
    boom::drums::generate(specB, bars, restPct, dottedPct, tripletPct, swingPct, /*seed*/ -1, patB);

    // --- Blend logic: combine candidate hits and sample per-weight ---
    std::mt19937 rng{ std::random_device{}() };
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);

    auto keyOf = [](int row, int startTick) -> int64_t
        {
            return ((int64_t)row << 32) ^ (int64_t)startTick;
        };

    struct Cand { int row, startTick, len, vel; enum Src { A, B, Both } src; };
    std::unordered_map<int64_t, Cand> candidates; candidates.reserve(patA.size() + patB.size());

    // Accumulate A
    for (const auto& e : patA)
    {
        const int64_t k = keyOf(e.row, e.startTick);
        candidates[k] = { e.row, e.startTick, e.lenTicks, juce::jlimit<int>(1,127,(int)e.vel), Cand::A };
    }
    // Accumulate B / mark collisions as Both
    for (const auto& e : patB)
    {
        const int64_t k = keyOf(e.row, e.startTick);
        auto it = candidates.find(k);
        if (it == candidates.end())
        {
            candidates[k] = { e.row, e.startTick, e.lenTicks, juce::jlimit<int>(1,127,(int)e.vel), Cand::B };
        }
        else
        {
            // keep max length / avg vel on collision; mark as Both
            it->second.len = std::max(it->second.len, (int)e.lenTicks);
            it->second.vel = (it->second.vel + juce::jlimit<int>(1, 127, (int)e.vel)) / 2;
            it->second.src = Cand::Both;
        }
    }

    BoomAudioProcessor::Pattern out;
    out.ensureStorageAllocated((int)candidates.size());

    for (const auto& kv : candidates)
    {
        const Cand& c = kv.second;
        float takeProb =
            (c.src == Cand::A) ? wA :
            (c.src == Cand::B) ? wB :
            // both present; pick by weights
            (uni(rng) < wA ? 1.0f : 0.0f);

        bool take = false;
        if (c.src == Cand::Both)
        {
            // For Both, we choose one deterministically by coin of weights
            take = true;
        }
        else
        {
            // For solo candidates, sample by weight so extremes 0%/100% behave intuitively
            take = (uni(rng) < takeProb);
        }

        if (!take) continue;

        BoomAudioProcessor::Note n;
        n.row = c.row;
        n.startTick = c.startTick;
        n.lengthTicks = c.len;
        n.velocity = c.vel;
        out.add(n);
    }

    setDrumPattern(out);
}

void BoomAudioProcessor::aiSlapsmithExpand(int bars)
{
    DBG("BoomAudioProcessor::aiSlapsmithExpand called bars=" << bars);

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

    DBG("BoomAudioProcessor::aiSlapsmithExpand -> setDrumPattern size=" << getDrumPattern().size());
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
// --- generate808: bars, octave offset, density %, triplets, dotted, seed ---
void BoomAudioProcessor::generate808(int bars,
    int octave,
    int densityPct,
    bool allowTriplets,
    bool allowDotted,
    int seed)
{

    // ----------------- guards & basic env -----------------
    bars = juce::jlimit(1, 8, bars);
    densityPct = juce::jlimit(0, 100, densityPct);

    // read key/scale
    juce::String keyName = "C";
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("key")))
        keyName = p->getCurrentChoiceName();

    juce::String scaleName = "Chromatic";
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("scale")))
        scaleName = p->getCurrentChoiceName();

    // time signature -> steps per bar
    auto tsParam = apvts.state.getProperty("timeSig").toString();
    if (tsParam.isEmpty()) tsParam = "4/4";
    auto tsParts = juce::StringArray::fromTokens(tsParam, "/", "");
    const int tsNum = tsParts.size() >= 1 ? tsParts[0].getIntValue() : 4;
    const int tsDen = tsParts.size() >= 2 ? tsParts[1].getIntValue() : 4;
    auto stepsPerBarFor = [&](int num, int den)->int
        {
            if (den == 4) return num * 4;
            if (den == 8) return num * 2;
            if (den == 16) return num;
            return num * 4;
        };
    const int stepsPerBar = stepsPerBarFor(tsNum, tsDen);
    const int totalSteps = stepsPerBar * bars;
    const int tps = kTicksPerStep; // your grid ticks-per-step (24)

    // scale pcs / degree->pitch helper (reuse pattern from file)
    auto itScale = kScales.find(scaleName.trim());
    const auto& scalePCs = (itScale != kScales.end()) ? itScale->second : kScales.at("Chromatic");
    const int scaleSize = (int)scalePCs.size();

    auto wrap12 = [](int v) { v %= 12; if (v < 0) v += 12; return v; };

    // find key index
    const int keyIndex = juce::jmax(0, kKeys.indexOf(keyName.trim().toUpperCase()));

    auto degreeToPitchLocal = [&](int degreeIndex, int oct) -> int
        {
            if (scaleSize == 0) return juce::jlimit(0, 127, oct * 12 + wrap12(keyIndex));
            int di = degreeIndex % scaleSize;
            if (di < 0) di += scaleSize;
            const int pc = scalePCs[di];
            return juce::jlimit(0, 127, oct * 12 + wrap12(keyIndex + pc));
        };

    auto snapToScaleClosest = [&](int midiPitch)->int
        {
            // Snap a raw pitch to the nearest pitch present in the chosen scale (within +/-6 semis)
            if (scaleSize == 0) return midiPitch;
            const int rootPC = wrap12(keyIndex + scalePCs[0]);
            const int pc = wrap12(midiPitch % 12);
            int midi = midiPitch;
            for (int d = 0; d <= 6; ++d)
            {
                if (std::find(scalePCs.begin(), scalePCs.end(), wrap12(pc + d - rootPC)) != scalePCs.end()) return midiPitch + d;
                if (std::find(scalePCs.begin(), scalePCs.end(), wrap12(pc - d - rootPC)) != scalePCs.end()) return midiPitch - d;
            }
            return midiPitch;
        };

    // RNG seeded by provided seed (or time when seed == -1)
    const auto now32 = juce::Time::getMillisecondCounter();
    const auto ticks64 = (std::uint64_t)juce::Time::getHighResolutionTicks();
    const auto nonce = genNonce_.fetch_add(1, std::memory_order_relaxed) + 1;
    const std::uint64_t mix = (std::uint64_t)now32 ^ (ticks64) ^ (std::uint64_t)nonce;
    const int rng_seed = (seed == -1) ? (int)(mix & 0x7fffffff) : seed;
    juce::Random rng(rng_seed);
    auto pct = [&](int prob)->bool { return rng.nextInt({ 100 }) < juce::jlimit(0, 100, prob); };

    // read rest density (UI slider restDensity808)
    int restPct = 0;
    if (auto* rp = apvts.getRawParameterValue("restDensity808"))
        restPct = juce::jlimit(0, 100, (int)juce::roundToInt(rp->load()));

    // phrase base octave
    int baseOct = 3 + octave;
    if (tsDen == 8) baseOct = juce::jmax(1, baseOct - 1);

    // Clear pattern container
    auto mp = getMelodicPattern();
    mp.clear();

    TickGuard guard;
    guard.bucketSize = 12; // prevents micro-clumping; tweak if needed


    // ----------------- Seed templates (16-step) -----------------
    // 1 == hit, 0 == rest. We'll mutate these by dropping / adding clusters.
    const std::vector<std::array<int, 16>> seeds = {
        // sparse bounce
        {1,0,0,0, 0,0,1,0,  1,0,0,0, 0,0,1,0},
        // pump + offbeat
        {1,0,1,0, 0,1,0,0,  1,0,1,0, 0,1,0,0},
        // modern trap-ish cluster-ready
        {1,0,0,1, 0,0,1,0,  1,0,0,1, 0,0,1,0},
        // staggered float
        {1,0,0,0, 1,0,1,0,  0,0,1,0, 1,0,0,0},
        // long gap with punctuated clusters
        {1,0,0,0, 0,0,0,0,  0,1,0,1, 0,0,0,1},
        // uptempo bounce
        {1,0,1,0, 1,0,0,1,  1,0,1,0, 1,0,0,1},
        { 1,0,0,0, 0,0,1,0,  0,1,0,0, 1,0,0,0 } };

    const std::vector<std::array<int, 16>> expansionSeeds = {
        {1,0,0,0, 0,0,1,1,  0,1,0,0, 0,0,1,0},
        {1,0,1,0, 0,0,0,0,  1,0,0,1, 0,1,0,0},
        {1,0,0,1, 0,0,1,0,  0,0,0,1, 1,0,0,0},
        {1,0,0,0, 0,1,0,0,  0,0,1,1, 0,0,0,1},
        {1,0,0,0, 0,0,1,0,  1,0,0,0, 0,0,1,1},

        {1,0,0,0, 1,0,0,1,  0,0,1,0, 1,0,0,0},
        {1,0,0,1, 0,0,0,1,  0,1,0,0, 1,0,0,0},
        {1,0,1,0, 0,0,1,0,  0,1,0,0, 0,0,1,0},
        {1,0,0,0, 0,1,0,1,  1,0,0,0, 0,0,0,1},
        {1,0,0,0, 0,0,1,0,  0,1,1,0, 0,0,0,1},

        {1,0,1,0, 0,1,0,0,  1,0,0,1, 0,0,1,0},
        {1,0,0,0, 0,0,0,0,  1,0,1,0, 0,1,0,1},
        {1,0,0,0, 0,0,1,0,  0,1,0,1, 1,0,0,0},
        {1,0,0,1, 0,1,0,0,  1,0,0,0, 0,1,0,0},
        {1,0,0,0, 1,0,0,0,  0,1,0,1, 0,0,1,0},

        {1,0,0,0, 0,1,0,0,  0,0,1,0, 1,0,1,0},
        {1,0,0,1, 0,0,1,0,  0,0,1,0, 0,1,0,0},
        {1,0,0,0, 0,1,1,0,  0,0,0,1, 1,0,0,0},
        {1,0,0,1, 0,0,1,0,  1,0,1,0, 0,0,0,1},
        {1,0,0,0, 0,0,1,0,  1,0,1,0, 0,1,0,0},

        {1,0,1,0, 0,0,0,1,  0,1,0,0, 1,0,0,0},
        {1,0,0,0, 0,1,0,0,  0,0,0,1, 1,0,1,0},
        {1,0,0,1, 0,1,0,0,  0,1,0,0, 0,0,1,1},
        {1,0,0,0, 0,0,1,1,  1,0,0,1, 0,0,0,0},
        {1,0,1,0, 0,1,0,0,  0,0,1,0, 1,0,0,1},

        {1,0,0,0, 0,0,1,0,  0,0,0,0, 0,1,1,1},
        {1,0,0,0, 0,1,0,0,  0,0,0,1, 0,1,0,1},
        {1,0,0,1, 0,0,1,0,  0,1,0,0, 1,0,0,0},
        {1,0,1,0, 0,0,1,0,  1,0,0,0, 0,1,0,0},
        {1,0,0,0, 0,0,0,1,  1,0,1,0, 0,0,1,0},

        {1,0,0,0, 0,0,1,0,  0,1,0,0, 1,1,0,0},
        {1,0,0,1, 0,0,0,0,  0,1,0,1, 1,0,0,0},
        {1,0,1,0, 0,0,0,1,  0,1,1,0, 0,0,0,1},
        {1,0,0,0, 1,0,0,1,  0,1,0,0, 1,0,0,0},
        {1,0,0,0, 0,1,0,1,  0,1,0,0, 0,0,1,0},

        {1,0,0,1, 0,0,1,0,  0,1,0,0, 0,1,1,0},
        {1,0,0,0, 0,0,0,0,  1,0,0,1, 0,1,0,1},
        {1,0,0,1, 0,1,0,0,  1,0,1,0, 0,0,0,0},
        {1,0,1,0, 0,0,0,1,  0,0,1,0, 1,0,0,1},
        {1,0,0,0, 0,1,1,0,  0,0,1,0, 1,0,0,0},

        {1,0,0,0, 0,1,0,0,  0,1,1,0, 0,0,1,0},
        {1,0,1,0, 0,0,1,0,  0,0,1,0, 0,1,0,1},
        {1,0,0,0, 1,0,0,1,  0,0,1,0, 0,0,0,1},
        {1,0,0,1, 0,1,0,1,  0,0,0,1, 1,0,0,0},
        {1,0,1,0, 0,1,0,0,  0,0,1,1, 1,0,0,0},

        {1,0,0,0, 0,1,0,0,  1,0,0,1, 0,0,1,0},
{1,0,0,1, 0,0,1,0,  1,0,0,0, 0,1,0,0},
{1,0,0,0, 0,0,1,0,  0,1,0,1, 1,0,0,0},
{1,0,1,0, 0,0,1,0,  0,0,0,1, 0,1,0,1},
{1,0,0,0, 0,1,0,1,  0,0,1,0, 1,0,0,0},

{1,0,0,1, 0,0,0,0,  1,0,0,1, 0,0,1,0},
{1,0,0,0, 0,0,1,1,  0,1,0,0, 1,0,0,0},
{1,0,0,1, 0,1,0,0,  0,0,1,0, 1,0,1,0},
{1,0,1,0, 0,0,0,1,  0,1,0,0, 0,0,1,0},
{1,0,0,0, 0,0,0,1,  1,0,0,0, 0,1,0,1},

{1,0,0,1, 0,0,1,0,  0,1,0,1, 0,0,0,0},
{1,0,0,0, 1,0,0,0,  0,1,0,0, 0,0,1,1},
{1,0,0,0, 0,1,0,0,  1,0,0,1, 0,1,0,0},
{1,0,1,0, 0,0,1,0,  1,0,0,0, 0,0,1,0},
{1,0,0,0, 0,0,1,0,  1,0,1,0, 0,1,0,0},

{1,0,0,1, 0,0,0,1,  0,0,1,0, 1,0,0,0},
{1,0,0,0, 0,1,1,0,  0,0,1,0, 1,0,0,1},
{1,0,1,0, 0,1,0,0,  1,0,0,0, 0,0,1,0},
{1,0,0,0, 0,0,1,0,  0,1,0,0, 1,0,1,0},
{1,0,0,1, 0,0,1,0,  1,0,1,0, 0,0,1,0},

{1,0,0,0, 0,1,0,0,  1,0,0,1, 0,0,0,1},
{1,0,0,1, 0,0,1,0,  0,1,0,0, 1,0,0,1},
{1,0,1,0, 0,0,0,0,  0,1,0,1, 1,0,0,0},
{1,0,0,0, 0,0,1,0,  0,1,1,0, 1,0,0,0},
{1,0,0,1, 0,1,0,0,  1,0,0,1, 0,0,0,1},

{1,0,1,0, 0,0,1,0,  0,0,1,0, 1,0,0,0},
{1,0,0,0, 0,1,0,0,  0,0,1,0, 1,0,0,0},
{1,0,0,1, 0,0,0,1,  1,0,0,0, 0,1,0,0},
{1,0,1,0, 0,1,0,0,  1,0,0,1, 0,0,0,0},
{1,0,0,0, 0,0,1,1,  0,0,1,0, 1,0,1,0},

{1,0,0,1, 0,0,1,0,  0,1,0,0, 0,0,1,0},
{1,0,1,0, 0,0,0,1,  1,0,0,0, 0,1,0,1},
{1,0,0,0, 1,0,0,1,  0,1,0,0, 0,0,1,0},
{1,0,0,1, 0,0,1,0,  1,0,0,1, 0,1,0,0},
{1,0,0,0, 0,1,0,1,  1,0,0,0, 0,1,0,0},

{1,0,0,1, 0,0,0,1,  0,1,1,0, 0,0,0,1},
{1,0,0,0, 0,0,1,0,  1,0,1,0, 0,0,1,0},
{1,0,1,0, 0,1,0,0,  0,0,1,0, 1,0,0,1},
{1,0,0,0, 0,1,0,0,  1,0,0,0, 0,0,1,0},
{1,0,0,1, 0,0,1,0,  0,1,0,0, 1,0,0,0},

{1,0,0,0, 0,0,1,0,  1,0,0,1, 0,1,0,0},
{1,0,1,0, 0,0,1,0,  0,0,1,0, 0,1,0,1},
{1,0,0,0, 0,1,0,0,  1,0,0,1, 0,0,0,1},
{1,0,0,1, 0,0,0,0,  1,0,1,0, 0,1,0,0},
{1,0,0,0, 0,0,1,1,  1,0,0,0, 0,1,0,0},

        { 1, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0 },
        { 1,0,0,1, 0,0,1,0,  0,1,0,0, 1,0,0,0 },
        { 1,0,1,0, 0,0,0,1,  0,0,1,0, 1,0,0,0 },
        { 1,0,0,0, 0,1,0,0,  1,0,0,1, 0,0,1,0 },
        { 1,0,0,1, 0,0,0,0,  1,0,1,0, 0,1,0,0 },

        { 1,0,0,0, 0,0,1,1,  0,1,0,0, 1,0,0,0 },
        { 1,0,0,1, 0,1,0,0,  1,0,0,0, 0,0,1,0 },
        { 1,0,1,0, 0,0,1,0,  1,0,0,1, 0,0,0,0 },
        { 1,0,0,0, 1,0,0,1,  0,0,1,0, 0,1,0,0 },
        { 1,0,0,1, 0,0,1,0,  0,1,0,1, 0,0,0,1 },

        { 1,0,1,0, 0,0,1,0,  0,0,0,1, 1,0,0,0 },
        { 1,0,0,0, 0,1,0,1,  0,0,1,0, 1,0,0,0 },
        { 1,0,0,1, 0,0,0,1,  0,1,0,0, 1,0,0,0 },
        { 1,0,1,0, 0,1,0,0,  0,0,1,0, 1,0,0,1 },
        { 1,0,0,0, 0,0,1,0,  1,0,0,0, 0,1,0,1 },

        { 1,0,0,1, 0,0,1,0,  1,0,1,0, 0,0,1,0 },
        { 1,0,0,0, 1,0,0,0,  0,1,0,0, 1,0,0,1 },
        { 1,0,0,1, 0,1,0,0,  0,0,1,1, 0,0,0,0 },
        { 1,0,1,0, 0,0,0,1,  1,0,0,0, 0,1,0,0 },
        { 1,0,0,0, 0,1,0,0,  0,0,1,0, 1,0,1,0 },

        { 1,0,0,1, 0,0,1,0,  0,0,1,0, 1,0,0,0 },
        { 1,0,1,0, 0,0,0,0,  1,0,0,1, 0,1,0,0 },
        { 1,0,0,0, 0,0,1,0,  1,0,0,0, 0,0,1,1 },
        { 1,0,0,1, 0,0,0,1,  0,1,0,0, 0,0,1,0 },
        { 1,0,1,0, 0,1,0,0,  1,0,0,0, 0,0,1,0 },

        { 1,0,0,0, 0,0,1,0,  0,1,0,1, 1,0,0,0 },
        { 1,0,0,1, 0,0,1,0,  1,0,0,1, 0,0,0,1 },
        { 1,0,0,0, 0,1,0,0,  1,0,1,0, 0,0,1,0 },
        { 1,0,1,0, 0,0,1,0,  0,1,0,0, 1,0,0,0 },
        { 1,0,0,1, 0,0,0,0,  1,0,0,1, 0,0,1,0 },

        { 1,0,0,0, 1,0,0,1,  0,1,0,0, 1,0,0,0 },
        { 1,0,0,1, 0,0,1,0,  0,0,1,0, 0,1,0,1 },
        { 1,0,1,0, 0,0,0,1,  0,1,0,0, 1,0,0,0 },
        { 1,0,0,0, 0,1,1,0,  0,0,1,0, 1,0,0,1 },
        { 1,0,0,1, 0,0,1,0,  1,0,0,0, 0,1,0,0 },

        { 1,0,0,0, 0,0,1,0,  1,0,1,0, 0,1,0,0 },
        { 1,0,1,0, 0,1,0,0,  0,0,1,0, 1,0,0,1 },
        { 1,0,0,0, 0,1,0,1,  0,0,1,0, 1,0,0,0 },
        { 1,0,0,1, 0,0,0,1,  0,1,0,0, 1,0,1,0 },
        { 1,0,0,0, 0,0,1,0,  0,1,0,0, 1,0,1,0 },

{ 1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1 },
{ 1,0,0,1, 0,0,0,0,  1,0,1,0, 0,1,0,0 },
{ 1,0,1,0, 0,0,1,0,  0,1,0,0, 1,0,0,0 },
{ 1,0,0,0, 0,0,1,1,  0,0,1,0, 1,0,0,0 },
{ 1,0,0,1, 0,1,0,0,  1,0,0,0, 0,0,1,0 },

{ 1,0,0,0, 1,0,0,1,  0,0,1,0, 1,0,0,0 },
{ 1,0,0,1, 0,0,1,0,  0,1,0,1, 0,0,0,0 },
{ 1,0,1,0, 0,0,0,1,  0,1,0,0, 1,0,0,1 },
{ 1,0,0,0, 0,1,0,0,  0,0,1,0, 1,0,1,0 },
{ 1,0,0,1, 0,0,1,0,  1,0,0,1, 0,0,0,1 },

{ 1,0,1,0, 0,1,0,0,  1,0,0,0, 0,0,1,0 },
{ 1,0,0,0, 0,0,1,0,  1,0,0,1, 0,1,0,0 },
{ 1,0,0,1, 0,0,0,1,  1,0,0,0, 0,1,0,0 },
{ 1,0,1,0, 0,0,0,0,  1,0,1,0, 0,0,1,0 },
{ 1,0,0,0, 0,0,1,0,  0,1,0,1, 1,0,0,0 },

{ 1,0,0,1, 0,0,1,0,  0,0,1,0, 1,0,0,0 },
{ 1,0,1,0, 0,0,1,0,  1,0,0,1, 0,0,0,0 },
{ 1,0,0,0, 0,1,0,1,  1,0,0,0, 0,0,1,0 },
{ 1,0,0,1, 0,1,0,0,  0,0,1,0, 1,0,0,1 },
{ 1,0,1,0, 0,0,1,1,  0,0,0,0, 1,0,0,0 },

{ 1,0,0,0, 0,0,1,0,  1,0,0,1, 0,1,0,0 },
{ 1,0,0,1, 0,0,0,0,  0,1,0,1, 1,0,0,0 },
{ 1,0,0,0, 1,0,0,0,  0,0,1,0, 0,1,0,1 },
{ 1,0,1,0, 0,0,0,1,  1,0,0,0, 0,0,1,0 },
{ 1,0,0,1, 0,0,1,0,  0,1,0,0, 1,0,0,0 },

{ 1,0,0,0, 0,1,1,0,  0,0,1,0, 1,0,0,0 },
{ 1,0,0,1, 0,0,1,0,  1,0,1,0, 0,0,0,1 },
{ 1,0,1,0, 0,0,0,1,  0,1,0,0, 1,0,0,0 },
{ 1,0,0,0, 0,0,1,0,  1,0,0,0, 0,1,0,1 },
{ 1,0,0,1, 0,1,0,0,  1,0,0,1, 0,0,0,0 },

{ 1,0,1,0, 0,0,1,0,  0,1,0,0, 1,0,0,0 },
{ 1,0,0,0, 1,0,0,1,  0,0,1,0, 0,0,1,0 },
{ 1,0,0,1, 0,0,0,0,  1,0,1,0, 0,1,0,0 },
{ 1,0,1,0, 0,1,0,0,  0,0,1,0, 1,0,0,1 },
{ 1,0,0,0, 0,0,1,1,  1,0,0,0, 0,1,0,0 },

{ 1,0,0,1, 0,0,1,0,  0,0,1,0, 1,0,0,0 },
{ 1,0,1,0, 0,0,0,1,  0,1,0,0, 1,0,0,1 },
{ 1,0,0,0, 0,1,0,0,  1,0,0,1, 0,0,1,0 },
{ 1,0,0,1, 0,0,1,0,  1,0,1,0, 0,0,0,1 },
{ 1,0,1,0, 0,0,1,0,  1,0,0,0, 0,1,0,1 },

{ 1,0,0,0, 0,0,1,0,  0,1,0,0, 1,0,1,0 },
{ 1,0,0,1, 0,1,0,0,  1,0,0,0, 0,0,1,0 },
{ 1,0,1,0, 0,0,0,1,  1,0,0,0, 0,1,0,0 },
{ 1,0,0,0, 1,0,0,1,  0,0,1,0, 0,0,0,1 },
{ 1,0,0,1, 0,0,1,0,  0,1,1,0, 1,0,0,0 },

        { 1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0, 0 },
        { 1,0,0,0, 0,1,0,1,  0,0,1,0, 1,0,0,0 },
        { 1,0,1,0, 0,0,0,1,  0,1,0,0, 1,0,0,1 },
        { 1,0,0,1, 0,0,1,0,  0,0,1,0, 1,0,0,1 },
        { 1,0,0,0, 1,0,0,1,  0,0,1,0, 0,1,0,0 },

        { 1,0,1,0, 0,0,1,0,  1,0,0,0, 0,0,1,1 },
        { 1,0,0,0, 0,0,1,0,  0,1,0,1, 1,0,0,0 },
        { 1,0,0,1, 0,1,0,0,  1,0,0,1, 0,0,0,0 },
        { 1,0,1,0, 0,0,0,0,  1,0,1,0, 0,0,1,0 },
        { 1,0,0,0, 0,0,1,1,  0,0,1,0, 1,0,0,0 },

        { 1,0,0,1, 0,0,0,1,  0,1,0,0, 1,0,1,0 },
        { 1,0,0,0, 0,1,0,0,  1,0,0,1, 0,0,1,0 },
        { 1,0,1,0, 0,0,1,0,  0,0,0,1, 1,0,0,0 },
        { 1,0,0,0, 0,1,0,1,  0,0,0,1, 1,0,0,0 },
        { 1,0,0,1, 0,0,1,0,  1,0,0,0, 0,1,0,0 },

        { 1,0,1,0, 0,0,1,0,  0,1,0,0, 1,0,0,0 },
        { 1,0,0,0, 1,0,0,0,  0,1,0,1, 0,0,1,0 },
        { 1,0,0,1, 0,0,0,1,  1,0,0,0, 0,0,1,0 },
        { 1,0,1,0, 0,0,0,1,  0,1,0,0, 1,0,0,0 },
        { 1,0,0,0, 0,0,1,0,  1,0,1,0, 0,1,0,0 },

        { 1,0,0,1, 0,0,1,0,  1,0,0,1, 0,0,0,1 },
        { 1,0,0,0, 0,1,1,0,  0,0,1,0, 1,0,0,0 },
        { 1,0,1,0, 0,1,0,0,  1,0,0,0, 0,0,1,0 },
        { 1,0,0,1, 0,0,0,0,  1,0,0,1, 0,1,0,0 },
        { 1,0,0,0, 0,0,1,0,  0,1,0,1, 1,0,0,1 },

        { 1,0,1,0, 0,0,1,0,  0,0,1,0, 1,0,0,0 },
        { 1,0,0,0, 1,0,0,1,  0,0,1,0, 0,1,0,1 },
        { 1,0,0,1, 0,0,1,0,  0,1,0,0, 1,0,0,0 },
        { 1,0,1,0, 0,0,0,1,  1,0,0,1, 0,0,0,0 },
        { 1,0,0,0, 0,1,0,1,  1,0,0,0, 0,0,1,0 },

        { 1,0,0,1, 0,1,0,0,  1,0,1,0, 0,0,1,0 },
        { 1,0,0,0, 0,0,1,0,  1,0,0,1, 0,1,0,0 },
        { 1,0,1,0, 0,0,0,1,  0,1,0,0, 1,0,0,1 },
        { 1,0,0,0, 1,0,0,1,  0,0,1,0, 0,1,0,0 },
        { 1,0,0,1, 0,0,1,0,  1,0,1,0, 0,0,0,1 },

        { 1,0,1,0, 0,0,1,0,  0,1,0,0, 1,0,0,0 },
        { 1,0,0,0, 0,1,0,0,  1,0,0,1, 0,0,1,0 },
        { 1,0,0,1, 0,0,0,1,  0,1,0,0, 0,0,1,1 },
        { 1,0,1,0, 0,0,1,0,  1,0,0,0, 0,1,0,1 },
        { 1,0,0,0, 0,0,1,1,  1,0,0,0, 0,1,0,0 },

        { 1,0,0,1, 0,0,1,0,  0,0,1,0, 1,0,0,0 },
        { 1,0,1,0, 0,1,0,0,  1,0,0,0, 0,0,1,0 },
        { 1,0,0,0, 1,0,0,1,  0,1,0,0, 1,0,0,0 },
        { 1,0,0,1, 0,0,0,1,  1,0,0,0, 0,0,1,0 },
        { 1,0,1,0, 0,0,1,0,  1,0,0,1, 0,0,0,1 }

    };

    std::vector<std::array<int, 16>> seeds_combined;
    seeds_combined.reserve(seeds.size() + expansionSeeds.size());
    seeds_combined.insert(seeds_combined.end(), seeds.begin(), seeds.end());
    seeds_combined.insert(seeds_combined.end(), expansionSeeds.begin(), expansionSeeds.end());
    std::vector<juce::String> seedNames;
    seedNames.reserve(seeds_combined.size());

    // first fill with names for the original seeds (adjust if your original list differed)
    seedNames.push_back("sparseBounce1"); seedNames.push_back("pumpOffbeat1"); seedNames.push_back("staggerPump1");
    seedNames.push_back("floatPocket1"); seedNames.push_back("longGapPunct1"); seedNames.push_back("uptempoBounce1");
    seedNames.push_back("offbeatSecondary1"); seedNames.push_back("doubleClusterMid1"); seedNames.push_back("syncopated1"); seedNames.push_back("spacedThirds1");
    // ... (you can expand these to match exact order of your original seeds)

    // now append names for expansionSeeds (50 names)
    seedNames.push_back("exp_01");
    seedNames.push_back("exp_02");
    seedNames.push_back("exp_03");
    seedNames.push_back("exp_04");
    seedNames.push_back("exp_05");
    seedNames.push_back("exp_06");
    seedNames.push_back("exp_07");
    seedNames.push_back("exp_08");
    seedNames.push_back("exp_09");
    seedNames.push_back("exp_10");
    seedNames.push_back("exp_11");
    seedNames.push_back("exp_12");
    seedNames.push_back("exp_13");
    seedNames.push_back("exp_14");
    seedNames.push_back("exp_15");
    seedNames.push_back("exp_16");
    seedNames.push_back("exp_17");
    seedNames.push_back("exp_18");
    seedNames.push_back("exp_19");
    seedNames.push_back("exp_20");
    seedNames.push_back("exp_21");
    seedNames.push_back("exp_22");
    seedNames.push_back("exp_23");
    seedNames.push_back("exp_24");
    seedNames.push_back("exp_25");
    seedNames.push_back("exp_26");
    seedNames.push_back("exp_27");
    seedNames.push_back("exp_28");
    seedNames.push_back("exp_29");
    seedNames.push_back("exp_30");
    seedNames.push_back("exp_31");
    seedNames.push_back("exp_32");
    seedNames.push_back("exp_33");
    seedNames.push_back("exp_34");
    seedNames.push_back("exp_35");
    seedNames.push_back("exp_36");
    seedNames.push_back("exp_37");
    seedNames.push_back("exp_38");
    seedNames.push_back("exp_39");
    seedNames.push_back("exp_40");
    seedNames.push_back("exp_41");
    seedNames.push_back("exp_42");
    seedNames.push_back("exp_43");
    seedNames.push_back("exp_44");
    seedNames.push_back("exp_45");
    seedNames.push_back("exp_46");
    seedNames.push_back("exp_47");
    seedNames.push_back("exp_48");
    seedNames.push_back("exp_49");
    seedNames.push_back("exp_50");
    seedNames.push_back("exp051");
    seedNames.push_back("exp052");
    seedNames.push_back("exp053");
    seedNames.push_back("exp054");
    seedNames.push_back("exp055");
    seedNames.push_back("exp056");
    seedNames.push_back("exp057");
    seedNames.push_back("exp058");
    seedNames.push_back("exp059");
    seedNames.push_back("exp060");
    seedNames.push_back("exp061");
    seedNames.push_back("exp062");
    seedNames.push_back("exp063");
    seedNames.push_back("exp064");
    seedNames.push_back("exp065");
    seedNames.push_back("exp066");
    seedNames.push_back("exp067");
    seedNames.push_back("exp068");
    seedNames.push_back("exp069");
    seedNames.push_back("exp070");
    seedNames.push_back("exp071");
    seedNames.push_back("exp072");
    seedNames.push_back("exp073");
    seedNames.push_back("exp074");
    seedNames.push_back("exp075");
    seedNames.push_back("exp076");
    seedNames.push_back("exp077");
    seedNames.push_back("exp078");
    seedNames.push_back("exp079");
    seedNames.push_back("exp080");
    seedNames.push_back("exp081");
    seedNames.push_back("exp082");
    seedNames.push_back("exp083");
    seedNames.push_back("exp084");
    seedNames.push_back("exp085");
    seedNames.push_back("exp086");
    seedNames.push_back("exp087");
    seedNames.push_back("exp088");
    seedNames.push_back("exp089");
    seedNames.push_back("exp090");
    seedNames.push_back("exp091");
    seedNames.push_back("exp092");
    seedNames.push_back("exp093");
    seedNames.push_back("exp094");
    seedNames.push_back("exp095");
    seedNames.push_back("exp096");
    seedNames.push_back("exp097");
    seedNames.push_back("exp098");
    seedNames.push_back("exp099");
    seedNames.push_back("exp100");
    seedNames.push_back("exp101");
    seedNames.push_back("exp102");
    seedNames.push_back("exp103");
    seedNames.push_back("exp104");
    seedNames.push_back("exp105");
    seedNames.push_back("exp106");
    seedNames.push_back("exp107");
    seedNames.push_back("exp108");
    seedNames.push_back("exp109");
    seedNames.push_back("exp110");
    seedNames.push_back("exp111");
    seedNames.push_back("exp112");
    seedNames.push_back("exp113");
    seedNames.push_back("exp114");
    seedNames.push_back("exp115");
    seedNames.push_back("exp116");
    seedNames.push_back("exp117");
    seedNames.push_back("exp118");
    seedNames.push_back("exp119");
    seedNames.push_back("exp120");
    seedNames.push_back("exp121");
    seedNames.push_back("exp122");
    seedNames.push_back("exp123");
    seedNames.push_back("exp124");
    seedNames.push_back("exp125");
    seedNames.push_back("exp126");
    seedNames.push_back("exp127");
    seedNames.push_back("exp128");
    seedNames.push_back("exp129");
    seedNames.push_back("exp130");
    seedNames.push_back("exp131");
    seedNames.push_back("exp132");
    seedNames.push_back("exp133");
    seedNames.push_back("exp134");
    seedNames.push_back("exp135");
    seedNames.push_back("exp136");
    seedNames.push_back("exp137");
    seedNames.push_back("exp138");
    seedNames.push_back("exp139");
    seedNames.push_back("exp140");
    seedNames.push_back("exp141");
    seedNames.push_back("exp142");
    seedNames.push_back("exp143");
    seedNames.push_back("exp144");
    seedNames.push_back("exp145");
    seedNames.push_back("exp146");
    seedNames.push_back("exp147");
    seedNames.push_back("exp148");
    seedNames.push_back("exp149");
    seedNames.push_back("exp150");
    seedNames.push_back("exp201");
    seedNames.push_back("exp202");
    seedNames.push_back("exp203");
    seedNames.push_back("exp204");
    seedNames.push_back("exp205");
    seedNames.push_back("exp206");
    seedNames.push_back("exp207");
    seedNames.push_back("exp208");
    seedNames.push_back("exp209");
    seedNames.push_back("exp210");
    seedNames.push_back("exp211");
    seedNames.push_back("exp212");
    seedNames.push_back("exp213");
    seedNames.push_back("exp214");
    seedNames.push_back("exp215");
    seedNames.push_back("exp216");
    seedNames.push_back("exp217");
    seedNames.push_back("exp218");
    seedNames.push_back("exp219");
    seedNames.push_back("exp220");
    seedNames.push_back("exp221");
    seedNames.push_back("exp222");
    seedNames.push_back("exp223");
    seedNames.push_back("exp224");
    seedNames.push_back("exp225");
    seedNames.push_back("exp226");
    seedNames.push_back("exp227");
    seedNames.push_back("exp228");
    seedNames.push_back("exp229");
    seedNames.push_back("exp230");
    seedNames.push_back("exp231");
    seedNames.push_back("exp232");
    seedNames.push_back("exp233");
    seedNames.push_back("exp234");
    seedNames.push_back("exp235");
    seedNames.push_back("exp236");
    seedNames.push_back("exp237");
    seedNames.push_back("exp238");
    seedNames.push_back("exp239");
    seedNames.push_back("exp240");
    seedNames.push_back("exp241");
    seedNames.push_back("exp242");
    seedNames.push_back("exp243");
    seedNames.push_back("exp244");
    seedNames.push_back("exp245");
    seedNames.push_back("exp246");
    seedNames.push_back("exp247");
    seedNames.push_back("exp248");
    seedNames.push_back("exp249");
    seedNames.push_back("exp250");
    seedNames.push_back("exp251");
    seedNames.push_back("exp252");
    seedNames.push_back("exp253");
    seedNames.push_back("exp254");
    seedNames.push_back("exp255");
    seedNames.push_back("exp256");
    seedNames.push_back("exp257");
    seedNames.push_back("exp258");
    seedNames.push_back("exp259");
    seedNames.push_back("exp260");
    seedNames.push_back("exp261");
    seedNames.push_back("exp262");
    seedNames.push_back("exp263");
    seedNames.push_back("exp264");
    seedNames.push_back("exp265");
    seedNames.push_back("exp266");
    seedNames.push_back("exp267");
    seedNames.push_back("exp268");
    seedNames.push_back("exp269");
    seedNames.push_back("exp270");
    seedNames.push_back("exp271");
    seedNames.push_back("exp272");
    seedNames.push_back("exp273");
    seedNames.push_back("exp274");
    seedNames.push_back("exp275");
    seedNames.push_back("exp276");
    seedNames.push_back("exp277");
    seedNames.push_back("exp278");
    seedNames.push_back("exp279");
    seedNames.push_back("exp280");
    seedNames.push_back("exp281");
    seedNames.push_back("exp282");
    seedNames.push_back("exp283");
    seedNames.push_back("exp284");
    seedNames.push_back("exp285");
    seedNames.push_back("exp286");
    seedNames.push_back("exp287");
    seedNames.push_back("exp288");
    seedNames.push_back("exp289");
    seedNames.push_back("exp290");
    seedNames.push_back("exp291");
    seedNames.push_back("exp292");
    seedNames.push_back("exp293");
    seedNames.push_back("exp294");
    seedNames.push_back("exp295");
    seedNames.push_back("exp296");
    seedNames.push_back("exp297");
    seedNames.push_back("exp298");
    seedNames.push_back("exp299");
    seedNames.push_back("exp300");

    const int chosenSeedIdx = rng.nextInt({ (int)seeds_combined.size() });
    const auto baseSeed = seeds_combined[chosenSeedIdx];

    // ----------------- Build step occupancy for totalSteps -----------------
    std::vector<int> stepHits(totalSteps, 0);

    // Apply seed per bar (repeat the 16-step seed across bars)
    for (int b = 0; b < bars; ++b)
    {
        for (int s = 0; s < stepsPerBar; ++s)
        {
            int val = (s < 16) ? baseSeed[s % 16] : 0;
            // mutate: sometimes shift seed a bit left/right to add variety
            if (rng.nextInt({ 100 }) < 12)
            {
                int shift = rng.nextBool() ? 1 : -1;
                int idx = juce::jlimit(0, 15, s + shift);
                val = baseSeed[idx];
            }
            stepHits[b * stepsPerBar + s] = val;
        }
    }

    // Enforce a hit on the very first step of the whole generated loop (only once)
    if (totalSteps > 0)
        stepHits[0] = 1;

    // Apply rest density: randomly remove hits to create gaps (higher restPct => more gaps)
    for (int i = 0; i < totalSteps; ++i)
    {
        if (stepHits[i] == 1)
        {
            // restPct raises chance to drop a given hit; clamp so we don't drop all
            const int rawDrop = (int)juce::roundToInt((float)restPct / 1.2f);
            const int dropChance = juce::jlimit(5, 85, rawDrop);
            if (rng.nextInt({ 100 }) < dropChance && i != 0) // never drop the first loop-downbeat
                stepHits[i] = 0;
        }
    }

    // Add cluster zones: for each bar choose 0..2 cluster centers and add neighboring hits
    for (int b = 0; b < bars; ++b)
    {
        const int clusters = rng.nextInt({ 3 }); // 0..2
        for (int c = 0; c < clusters; ++c)
        {
            const int center = b * stepsPerBar + rng.nextInt({ stepsPerBar });
            for (int offset = -2; offset <= 2; ++offset)
            {
                int idx = center + offset;
                if (idx >= b * stepsPerBar && idx < (b + 1) * stepsPerBar)
                {
                    // probability of adding cluster note decays with distance
                    const int prob = juce::jmax(15, 70 - std::abs(offset) * 25);
                    if (rng.nextInt({ 100 }) < prob)
                        stepHits[idx] = 1;
                }
            }
        }
    }

    // Prevent too many consecutive hits: if you see >4 consecutive, drop some to keep groove
    for (int i = 0; i < totalSteps; ++i)
    {
        int run = 0;
        for (int j = i; j < juce::jmin(totalSteps, i + 8); ++j)
        {
            if (stepHits[j] == 1) ++run; else break;
        }
        if (run > 4)
        {
            for (int k = i + 2; k < i + run; k += 2)
            {
                if (k < totalSteps && rng.nextInt({ 100 }) < 60) stepHits[k] = 0;
            }
        }
    }

    // ----------------- Place notes with pitch intelligence -----------------
    // Pitch rules:
    // - 70% root
    // - 15% fifth
    // - 10% octave jump (up or down)
    // - 5% approach (root +/- 1 semitone snapped to scale)
    const int rootDegree = 0;
    const int fifthDegree = juce::jmin(4, scaleSize - 1); // approx 5th within scale; safe if small scale
    for (int step = 0; step < totalSteps; ++step)
    {
        if (stepHits[step] == 0) continue;

        // base tick
        int startTick = step * tps;

        // choose velocity
        int vel = 80 + (int)rng.nextInt({ 24 }); // 80..103

        // pick pitch choice by weights
        int pitchMidi = degreeToPitchLocal(rootDegree, baseOct);

        const int r = rng.nextInt({ 100 });
        if (r < 70)
        {
            // root
            pitchMidi = degreeToPitchLocal(rootDegree, baseOct);
        }
        else if (r < 85)
        {
            // fifth
            pitchMidi = degreeToPitchLocal(fifthDegree, baseOct);
        }
        else if (r < 95)
        {
            // octave jump (either +12 or -12)
            const int dir = rng.nextBool() ? 1 : -1;
            pitchMidi = juce::jlimit(0, 127, degreeToPitchLocal(rootDegree, baseOct + dir));
        }
        else
        {
            // approach note: root +/- 1 semitone then snap to scale
            int approx = degreeToPitchLocal(rootDegree, baseOct);
            int plusminus = rng.nextBool() ? 1 : -1;
            pitchMidi = snapToScaleClosest(approx + plusminus);
        }

        // length in ticks (respect triplet/dotted options)
        int lenTicks = tps; // default 1 step
        if (allowTriplets && pct(25)) lenTicks = (tps * 2) / 3;
        if (allowDotted && pct(10)) lenTicks = juce::jmax(1, (int)juce::roundToInt((double)lenTicks * 1.5));

        // add main note
        mp.add({ pitchMidi, startTick, juce::jmax(6, lenTicks), vel });

        // occasional 1/32 split/roll (short tap after main note)
        const float splitProb = 0.16f; // modest rate
        if (rng.nextFloat() < splitProb)
        {
            const int splitTick = juce::jmin(totalSteps * tps - 1, startTick + (tps / 2)); // +12 ticks
            const int vel2 = juce::jlimit(10, 127, vel - 18);
            int pitch2 = pitchMidi;
            // small chance to octave-shift the roll for motion
            if (rng.nextInt({ 100 }) < 20)
                pitch2 = juce::jlimit(0, 127, pitchMidi + (rng.nextBool() ? 12 : -12));
            mp.add({ pitch2, splitTick, juce::jmax(4, tps / 2), vel2 });
        }

        // occasional octave jump: extra note placed immediately after to create movement
        if (rng.nextInt({ 100 }) < 8)
        {
            const int afterTick = juce::jmin(totalSteps * tps - 1, startTick + tps);
            int pitchUp = juce::jlimit(0, 127, pitchMidi + 12);
            mp.add({ pitchUp, afterTick, juce::jmax(6, tps / 2), juce::jlimit(20, 127, vel - 6) });
        }
    }

    // safety: if mp empty (very high rest), force a root downbeat
    if (mp.size() == 0)
    {
        mp.add({ degreeToPitchLocal(rootDegree, baseOct), 0, tps * 2, 110 });
    }

    // commit back into processor
    setMelodicPattern(mp);

    // repaint editor if available
    if (auto* ed = getActiveEditor()) ed->repaint();
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
    const int totalSteps = stepsPerBar * bars;                     // ticks per 1/16 step (your grid)                       // for export (not used here)

    // ---- Scale tables ----

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

void BoomAudioProcessor::bumppitTranspose(int targetKeyIndex, int octaveDelta)
{

    {
        // Only act if 808 or Bass engine is selected
        auto eng = getEngineSafe();
        // Melodic-only: proceed for any non-Drums engine (covers 808 and Bass without naming them)
        if (eng == boom::Engine::Drums)
            return;

        // Guard inputs
        targetKeyIndex = juce::jlimit(0, 11, targetKeyIndex);
        octaveDelta = juce::jlimit(-4, 4, octaveDelta);

        // Get the plugin's current key index from APVTS
        int currentKeyIndex = 0;
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("key")))
        {
            currentKeyIndex = p->getIndex();
        }

        // Calculate the difference in semitones
        int semitoneDifference = targetKeyIndex - currentKeyIndex;

        // Calculate the total shift including octaves
        int totalShift = semitoneDifference + (octaveDelta * 12);

        // Apply the shift to every note in the melodic pattern
        auto mp = getMelodicPattern();


        for (auto& n : mp)
        {
            n.pitch = juce::jlimit(0, 127, n.pitch + totalShift);
        }

        setMelodicPattern(mp);
        notifyEditor(*this);
    }
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

juce::MidiMessageSequence BoomAudioProcessor::generateRolls(const juce::String& style,
    int tsNum, int tsDen, int bars,
    bool allowTriplets, bool allowDotted,
    int seed) const
{
    // RNG (deterministic when seed provided)
    juce::Random rng(seed == -1 ? (int)juce::Time::getMillisecondCounter() : seed);
    juce::MidiMessageSequence seq;

    // clamp bars and compute steps
    const int clampedBars = juce::jlimit(1, 8, bars);
    const int stepsPerBar = stepsPerBarFromTS(tsNum, tsDen); // uses your project helper (16th-step base)
    const int totalSteps = stepsPerBar * clampedBars;

    // ---------- 100 roll seeds (16-step templates) ----------
    // Each entry is a 16-step template. These are denser than hat seeds and emphasize clusters/rolls.
    static const std::vector<std::array<int, 16>> rollSeeds = {
        // 1..16 - short dense bursts / classic rolls
        {1,1,1,0, 1,1,1,0,  1,1,1,0, 1,1,1,0},
        {1,1,0,1, 1,1,0,1,  1,1,0,1, 1,1,0,1},
        {1,0,1,0, 1,0,1,0,  1,0,1,0, 1,0,1,0},
        {1,1,1,1, 0,0,0,0,  1,1,1,1, 0,0,0,0},
        {1,0,0,1, 1,1,0,1,  1,0,0,1, 1,1,0,1},
        {1,1,0,0, 1,1,0,0,  1,1,0,0, 1,1,0,0},
        {1,1,1,0, 0,1,1,0,  0,1,1,0, 1,1,1,0},
        {1,0,1,1, 0,1,0,1,  1,1,0,1, 0,1,1,0},
        {1,1,0,1, 1,0,1,1,  1,1,0,1, 1,0,1,1},
        {1,1,1,0, 1,0,1,0,  1,1,1,0, 1,0,1,0},

        // 17..32 - accents and gaps
        {1,0,1,0, 1,1,0,0,  1,0,1,0, 1,0,1,0},
        {1,1,0,0, 1,0,1,0,  1,0,1,0, 1,1,0,0},
        {1,0,0,1, 0,1,1,1,  0,1,0,1, 0,1,1,1},
        {1,1,1,0, 1,0,0,1,  1,1,0,0, 1,0,1,0},
        {1,0,1,0, 0,1,1,0,  1,0,1,0, 0,1,1,0},
        {1,0,1,1, 1,0,1,0,  1,0,1,1, 1,0,1,0},
        {1,1,0,1, 0,1,0,1,  1,1,0,1, 0,1,0,1},
        {1,0,0,0, 1,1,1,1,  0,0,0,0, 1,1,1,1},
        {1,1,0,0, 0,1,1,0,  1,0,1,0, 0,1,1,0},
        {1,0,1,0, 1,0,1,1,  1,0,1,0, 1,0,1,1},

        // 33..48 - fast fills and trip-like spikes
        {1,1,1,1, 1,0,0,1,  1,1,1,1, 1,0,0,1},
        {1,1,0,1, 1,1,0,1,  0,1,1,0, 1,1,0,1},
        {1,0,1,0, 1,1,1,0,  1,0,1,0, 1,1,1,0},
        {1,1,1,0, 1,1,0,0,  1,1,1,0, 1,1,0,0},
        {1,0,1,1, 1,1,0,1,  1,0,1,1, 1,1,0,1},
        {1,1,0,0, 1,0,1,1,  1,1,0,0, 1,0,1,1},
        {1,0,1,0, 1,0,1,1,  1,0,1,0, 1,0,1,1},
        {1,1,1,0, 0,1,1,1,  1,1,1,0, 0,1,1,1},
        {1,0,1,0, 1,1,0,0,  1,1,1,0, 1,0,1,0},
        {1,1,1,1, 0,1,0,1,  1,1,1,1, 0,1,0,1},

        // 49..64 - long sustained roll templates (dense)
        {1,1,1,1, 1,1,1,1,  1,1,1,1, 1,1,1,1},
        {1,1,1,0, 1,1,1,0,  1,1,1,0, 1,1,1,0},
        {1,1,0,1, 1,1,0,1,  1,1,0,1, 1,1,0,1},
        {1,0,1,1, 1,0,1,1,  1,0,1,1, 1,0,1,1},
        {1,1,1,0, 0,1,1,1,  1,1,1,0, 0,1,1,1},
        {1,1,0,0, 1,1,0,0,  1,1,0,0, 1,1,0,0},
        {1,0,1,0, 1,0,1,0,  1,0,1,0, 1,0,1,0},
        {1,1,1,1, 1,0,1,0,  1,1,1,1, 1,0,1,0},
        {1,0,1,1, 0,1,1,0,  1,0,1,1, 0,1,1,0},
        {1,1,0,1, 1,0,1,1,  1,1,0,1, 1,0,1,1},

        // 65..80 - accents + gaps + flam opportunities
        {1,0,0,1, 1,0,0,1,  1,0,0,1, 1,0,0,1},
        {1,0,1,0, 1,1,0,1,  0,1,0,1, 1,0,1,0},
        {1,1,0,0, 0,1,1,1,  1,0,0,1, 1,1,0,0},
        {1,0,1,1, 0,0,1,0,  1,0,1,1, 0,0,1,0},
        {1,1,1,0, 1,0,0,0,  1,1,1,0, 1,0,0,0},
        {1,0,1,0, 0,1,0,1,  1,0,1,0, 0,1,0,1},
        {1,1,0,1, 1,1,0,0,  1,1,0,1, 1,1,0,0},
        {1,0,0,1, 0,1,1,0,  1,0,0,1, 0,1,1,0},
        {1,1,1,1, 0,0,1,0,  1,1,1,1, 0,0,1,0},
        {1,0,1,0, 1,0,1,1,  1,0,1,0, 1,0,1,1},

        // 81..96 - syncopated and funky roll motifs
        {1,0,1,0, 0,1,1,0,  1,1,0,1, 0,1,0,0},
        {1,1,0,1, 0,1,0,1,  1,0,1,1, 0,1,0,1},
        {1,0,0,1, 1,1,0,1,  0,1,1,0, 1,0,0,1},
        {1,1,1,0, 1,0,1,0,  0,1,0,1, 1,1,1,0},
        {1,0,1,1, 1,0,0,1,  1,0,1,1, 1,0,0,1},
        {1,1,0,0, 1,1,1,0,  0,1,1,1, 1,0,0,0},
        {1,0,1,0, 1,1,0,1,  0,0,1,1, 1,1,0,1},
        {1,1,1,1, 1,0,1,1,  1,1,1,1, 1,0,1,1},
        {1,0,0,0, 1,1,1,1,  1,0,0,0, 1,1,1,1},
        {1,1,0,1, 0,0,1,0,  1,1,0,1, 0,0,1,0},

        // 97..100 - sparse motifs for dramatic builds
        {1,0,0,0, 0,0,1,0,  0,0,0,0, 0,0,1,0},
        {1,0,0,1, 0,0,0,1,  0,0,0,1, 0,0,0,1},
        {1,0,1,0, 0,0,1,0,  0,0,1,0, 0,0,1,0},
        {1,1,0,0, 0,0,0,0,  0,1,1,0, 0,0,0,0}
    };

    // ---------- Append 200 programmatic roll seeds to reach 300 total ----------
// Build mutable copy of the static rollSeeds you defined earlier
    std::vector<std::array<int, 16>> allRollSeeds;
    allRollSeeds.reserve(300);
    allRollSeeds.insert(allRollSeeds.end(), rollSeeds.begin(), rollSeeds.end());

    // Deterministic generator for additional seeds (so paste -> same seeds each time)
    // Use a fixed salt so patterns are deterministic across runs/pastes
    juce::Random genExtra(0xB00F1234);

    // Helper to create a pattern with controlled density/shape
    auto makeVariantSeed = [&](int idx)->std::array<int, 16> {
        std::array<int, 16> s{};
        // base density oscillates by idx so we get variety (sparser -> denser)
        int baseDensity = 6 + (idx % 6); // ranges 6..11 (out of 16)
        // pick a motif shape selector
        int motif = (idx * 37) & 7;

        // fill using a few different motif generators for variety
        if (motif == 0)
        {
            // staggered clusters
            for (int i = 0; i < 16; ++i) s[i] = ((i % 4) == 0 || (genExtra.nextInt({ 100 }) < baseDensity * 6)) ? 1 : 0;
        }
        else if (motif == 1)
        {
            // accent anti-phase
            for (int i = 0; i < 16; ++i) s[i] = ((i % 3) == 0 || (i % 5) == 0) ? 1 : 0;
            // randomly drop some to create gaps
            for (int i = 0; i < 16; ++i) if (genExtra.nextInt({ 100 }) < 12) s[i] = 0;
        }
        else if (motif == 2)
        {
            // dense runs + spaced gaps
            for (int i = 0; i < 16; ++i) s[i] = (i % 2 == 0) ? 1 : (genExtra.nextInt({ 100 }) < 25 ? 1 : 0);
        }
        else if (motif == 3)
        {
            // triplet-ish accents
            for (int i = 0; i < 16; ++i) s[i] = ((i % 3) == 0) ? 1 : (genExtra.nextInt({ 100 }) < 18 ? 1 : 0);
        }
        else if (motif == 4)
        {
            // pairs + occasional long tails
            for (int i = 0; i < 16; i += 2) { s[i] = 1; if (genExtra.nextInt({ 100 }) < 60) s[i + 1] = 1; }
            if (genExtra.nextInt({ 100 }) < 25)
            {
                int pos = genExtra.nextInt({ 12 });
                s[pos] = s[(pos + 1) % 16] = s[(pos + 2) % 16] = 1;
            }
        }
        else if (motif == 5)
        {
            // sparse build
            for (int i = 0; i < 16; ++i) s[i] = (genExtra.nextInt({ 100 }) < (baseDensity * 4)) ? 1 : 0;
        }
        else if (motif == 6)
        {
            // syncopated 5/8 feel compressed into 16
            for (int i = 0; i < 16; ++i) s[i] = ((i % 5) == 0 || (i % 8) == 3) ? 1 : (genExtra.nextInt({ 100 }) < 10 ? 1 : 0);
        }
        else // motif==7
        {
            // very dense with occasional zero holes
            for (int i = 0; i < 16; ++i) s[i] = (genExtra.nextInt({ 100 }) < 78) ? 1 : 0;
            // carve a couple holes
            for (int h = 0; h < 2; ++h) s[genExtra.nextInt({ 16 })] = 0;
        }

        // small micro-variation per-index: rotate pattern by a small offset
        int rot = (idx * 3) % 16;
        std::array<int, 16> rotated{};
        for (int i = 0; i < 16; ++i) rotated[i] = s[(i + rot) % 16];
        return rotated;
        };

    // create and append 200 additional seeds (indices correspond to current size..size+199)
    int startIdx = (int)allRollSeeds.size();
    for (int i = 0; i < 200; ++i)
    {
        allRollSeeds.push_back(makeVariantSeed(startIdx + i));
    }

    // ---------- Build seedNames for all 300 seeds (explicit push_back calls) ----------
    std::vector<juce::String> seedNames;
    seedNames.reserve((int)allRollSeeds.size());
    for (int i = 0; i < (int)allRollSeeds.size(); ++i)
    {
        // build the name and push_back (this loop produces exact same effect as many manual push_back lines)
        seedNames.push_back("rollSeed_" + juce::String(i));
    }

    // ---------- Example: picking a seed index from the new allRollSeeds (replace earlier chosenIdx use) ----------
    const int chosenSeedIdx = rng.nextInt({ (int)allRollSeeds.size() }); // 0 .. 299
    const auto& chosenSeedTemplate = allRollSeeds[chosenSeedIdx];

    // (When converting to stepHits below, use chosenSeedTemplate instead of rollSeeds[chosenIdx])
    // e.g. for (int i=0;i<totalSteps;++i) stepHits[i] = chosenSeedTemplate[i % 16];
    DBG("generateRolls: using seed index " + juce::String(chosenSeedIdx) + " name=" + seedNames[chosenSeedIdx]);


    // ---------- Helper lambdas ----------
    auto coinInt = [&](int p)->bool { return rng.nextInt({ 100 }) < juce::jlimit(0, 100, p); };
    auto chooseMajorityLambda = [&]() -> int {
        int r = rng.nextInt({ 100 });
        bool isDrill = style.equalsIgnoreCase("drill");
        if (isDrill)
        {
            if (r < 25) return 0;
            if (r < 50) return 1;
            if (r < 75) return 3;
            if (r < 95) return 4;
            return 5;
        }
        if (r < 35) return 0;
        if (r < 70) return 1;
        if (r < 82) return 5;
        if (r < 90) return 3;
        if (r < 96) return 4;
        return 2;
        };
    auto majorityToTick = [&](int majority)->int {
        switch (majority)
        {
        case 0: return kT8;
        case 1: return kT16;
        case 2: return kT32;
        case 3: return kT8T;
        case 4: return kT16T;
        case 5: return kT4;
        default: return kT16;
        }
        };

    // ---------- Hybrid decision: per-style seed affinity ----------
    float styleSeedAffinity = 0.5f;
    juce::String s = style.toLowerCase();
    if (s == "wxstie") styleSeedAffinity = 0.85f; // prefer seeds for idiosyncratic style
    else if (s == "trap") styleSeedAffinity = 0.35f; // algorithmic favors
    else if (s == "drill") styleSeedAffinity = 0.30f;
    else if (s == "r&b") styleSeedAffinity = 0.60f;
    else if (s == "pop") styleSeedAffinity = 0.45f;
    else styleSeedAffinity = 0.50f;

    const bool useSeeds = (rng.nextFloat() < styleSeedAffinity);

    // ---------- Build initial stepHits ----------
    std::vector<int> stepHits(totalSteps, 0);
    int majority = chooseMajorityLambda();
    if (!allowTriplets && (majority == 3 || majority == 4)) majority = 1;
    const int majorityTick = majorityToTick(majority);

    if (useSeeds)
    {
        const int chosenIdx = rng.nextInt({ (int)rollSeeds.size() });
        const auto& base = rollSeeds[chosenIdx];
        for (int i = 0; i < totalSteps; ++i)
            stepHits[i] = base[i % 16];

        DBG("generateRolls: SEED path (idx=" + juce::String(chosenIdx) + ")");
    }
    else
    {
        // Algorithmic behavior for rolls: choose a base density and populate bursts
        // stride controls coarse grain: 1 -> every step, 2 -> every other, etc.
        int stride = 1;
        switch (majority)
        {
        case 0: stride = 2; break; // 8th-based feels -> sparser
        case 1: stride = 1; break; // 16th base -> normal
        case 2: stride = 1; break; // 32nd -> very dense
        case 3: stride = 2; break;
        case 4: stride = 1; break;
        case 5: stride = 4; break; // quarter -> very long hits
        default: stride = 1; break;
        }

        // Fill steady skeleton
        for (int i = 0; i < totalSteps; ++i)
            if (i % stride == 0)
                stepHits[i] = 1;

        // Add densification: cluster around selected centers (builds)
        int clustersPerBar = juce::jlimit(1, 5, 1 + rng.nextInt({ 3 }));
        for (int bar = 0; bar < clampedBars; ++bar)
        {
            int base = bar * stepsPerBar;
            for (int c = 0; c < clustersPerBar; ++c)
            {
                int center = base + rng.nextInt({ juce::jmax(1, stepsPerBar) });
                int len = rng.nextInt({ 2,6 });
                for (int r = 0; r < len; ++r)
                    if (center + r < totalSteps) stepHits[center + r] = 1;
            }
        }

        // Small chance of dramatic dense run (for fills)
        if (coinInt(18))
        {
            int start = rng.nextInt({ juce::jmax(1, totalSteps) });
            int run = juce::jmin(totalSteps - start, rng.nextInt({ 4,10 }));
            for (int r = 0; r < run; ++r) stepHits[start + r] = 1;
        }

        DBG("generateRolls: ALGO path (majority=" + juce::String(majority) + ")");
    }

    // ---------- Phrase-based mutation: gappy/wild/rotation ----------
    // choose path (0 steady, 1 gappy, 2 wild)
    int path = 0;
    {
        int r = rng.nextInt({ 100 });
        if (r < 45) path = 0;
        else if (r < 75) path = 1;
        else path = 2;
    }

    // small random rotate to vary phrase anchor
    if (rng.nextInt({ 100 }) < 12)
    {
        int shift = rng.nextBool() ? 1 : -1;
        std::vector<int> tmp(totalSteps);
        for (int i = 0; i < totalSteps; ++i)
            tmp[i] = stepHits[(i + shift + totalSteps) % totalSteps];
        stepHits.swap(tmp);
    }

    // per-bar mutation
    for (int bar = 0; bar < clampedBars; ++bar)
    {
        int base = bar * stepsPerBar;
        bool gappy = (path == 1);
        bool wild = (path == 2);

        if (gappy)
        {
            // remove some hits to create space
            for (int s = 0; s < stepsPerBar; ++s)
            {
                int idx = base + s;
                if (idx < totalSteps && stepHits[idx] == 1 && coinInt(30))
                    stepHits[idx] = 0;
            }
        }

        if (wild)
        {
            // randomly densify small pockets and micro-shift groups
            for (int s = 0; s < stepsPerBar; ++s)
            {
                int idx = base + s;
                if (idx < totalSteps)
                {
                    if (stepHits[idx] == 0 && coinInt(14)) stepHits[idx] = 1;
                    if (coinInt(10) && s < stepsPerBar - 1)
                        std::swap(stepHits[idx], stepHits[idx + 1]);
                }
            }
        }

        // clusters insertion 0..2 per bar
        int clusters = rng.nextInt({ 3 });
        for (int c = 0; c < clusters; ++c)
        {
            int center = base + rng.nextInt({ juce::jmax(1, stepsPerBar) });
            for (int off = -2; off <= 2; ++off)
            {
                int idx = center + off;
                if (idx >= base && idx < base + stepsPerBar && idx < totalSteps)
                {
                    int prob = juce::jmax(20, 80 - std::abs(off) * 20);
                    if (rng.nextInt({ 100 }) < prob) stepHits[idx] = 1;
                }
            }
        }

        // short roll/fill at end of bar (wild increases probability)
        if (wild || coinInt(22))
        {
            int pos = base + stepsPerBar - 1;
            int fills = rng.nextInt({ 1,5 });
            for (int f = 0; f < fills; ++f)
            {
                int idx = pos - f;
                if (idx >= base && idx < totalSteps) stepHits[idx] = 1;
            }
        }
    }

    // ---------- Convert stepHits -> snare MIDI events ----------
    const int snareNote = 38; // GM acoustic snare
    const int channel = 10;
    int vMin = 70, vMax = 120;
    if (s == "trap" || s == "drill") { vMin = 60; vMax = 110; }

    // snare roll helper (fast micro-division)
    auto addSnareRoll = [&](int t0, int len, bool fast) {
        int sub = fast ? kT32 : kT16;
        int t = t0;
        // crescendo/decrescendo across the roll can be faked by velocity ramp
        int steps = juce::jmax(1, (len + sub - 1) / sub);
        for (int i = 0; i < steps && t < t0 + len; ++i)
        {
            int vel = juce::jlimit(1, 127, vMin + (int)((float)(vMax - vMin) * ((float)i / (float)juce::jmax(1, steps - 1))));
            addNote(seq, t, juce::jmax(6, sub / 2), snareNote, vel, channel);
            t += sub;
        }
        };

    // flam helper: immediate grace note slightly before main hit
    auto addFlam = [&](int tMain, int mainLen, int mainVel) {
        int flamOffsetTicks = juce::jmax(1, kT16 / 6); // small offset
        int tFlam = juce::jmax(0, tMain - flamOffsetTicks);
        int flamVel = juce::jmax(8, juce::jmin(127, mainVel - rng.nextInt({ 10, 24 })));
        addNote(seq, tFlam, juce::jmax(4, flamOffsetTicks), snareNote, flamVel, channel);
        addNote(seq, tMain, juce::jmax(10, mainLen / 2), snareNote, mainVel, channel);
        };

    // iterate steps and output notes (flams, rolls, velocity shaping)
    for (int step = 0; step < totalSteps; ++step)
    {
        if (stepHits[step] == 0) continue;

        int tick = step * kT16;
        int len = majorityToTick(majority);
        if (allowDotted && coinInt(12)) len = (int)juce::roundToInt(len * 1.5f);

        int baseVel = rng.nextInt({ vMax - vMin + 1 }) + vMin;

        // accent rule: stronger on bar boundaries or first events of clusters
        if ((step % stepsPerBar) == 0 && coinInt(60))
            baseVel = juce::jmin(127, baseVel + 18);
        else if (coinInt(35))
            baseVel = juce::jmin(127, baseVel + 10);

        // decide roll vs single (style influenced and wild path)
        bool doRoll = false;
        if ((s == "r&b" || s == "trap" || s == "drill") && coinInt((path == 2) ? 65 : 30))
            doRoll = true;

        // slightly higher chance to flam on seed-sourced patterns (gives human feel)
        bool doFlam = (!useSeeds && coinInt(8)) || (useSeeds && coinInt(20));

        if (doRoll)
        {
            // roll length bias: short (tight) for trap/drill, longer for r&b/pop
            bool fast = (s == "trap" || s == "drill") ? true : coinInt(60);
            addSnareRoll(tick, len, fast);
        }
        else if (doFlam && coinInt(50))
        {
            addFlam(tick, len, baseVel);
        }
        else
        {
            addNote(seq, tick, juce::jmax(8, len / 2), snareNote, baseVel, channel);
        }

        // optional trailing ghost for motion
        if (coinInt(12))
        {
            int nTick = juce::jmin(totalSteps * kT16 - 1, tick + kT16 / 2);
            addNote(seq, nTick, juce::jmax(4, kT16 / 4), snareNote, juce::jmax(6, baseVel - 28), channel);
        }
    }

    // safety fallback: one soft snare on downbeat if empty
    if (seq.getNumEvents() == 0)
    {
        addNote(seq, 0, juce::jmax(8, kT16 / 2), snareNote, 80, channel);
    }

    // add tempo meta (harmless)
    seq.addEvent(juce::MidiMessage::tempoMetaEvent(0.5), 0); // default 120BPM
    seq.updateMatchedPairs();
    return seq;
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

namespace {
    constexpr int kPPQ_ = 96; // ticks per quarter
    constexpr int kT16_ = kPPQ / 4; // 24
    constexpr int kT8_ = kPPQ / 2; // 48
    constexpr int kT4_ = kPPQ; // 96
    constexpr int kT8T_ = kPPQ / 3; // 32 (8th-triplet)
    constexpr int kT16T_ = kT8 / 3; // 16 (16th-triplet)
    constexpr int kT32_ = kT16 / 2; // 12

    inline int stepsPerBarFromTS(int num, int den)
    {
        // Use 16th-step grid as the “step”. 4/4 => 16, 3/4 => 12, 6/8 => 12, 7/8 => 14, etc.
        if (den == 4) return num * 4;
        if (den == 8) return num * 2;
        if (den == 16) return num;
        return num * 4;
    }

    inline int tickForStep(int stepSize /*in 16th steps*/)
    {
        // 1 step = 16th = 24 ticks
        return stepSize * kT16_;
    }

    inline void addNote(juce::MidiMessageSequence& seq, int startTick, int lenTicks,
        int midiNote, int vel, int channel /*1..16*/)
    {
        const int ch = juce::jlimit(1, 16, channel);
        vel = juce::jlimit(1, 127, vel);
        seq.addEvent(juce::MidiMessage::noteOn(ch, midiNote, (juce::uint8)vel), startTick);
        seq.addEvent(juce::MidiMessage::noteOff(ch, midiNote), startTick + juce::jmax(3, lenTicks));
    }

    inline bool coin(juce::Random& r, int pct) { return r.nextInt({ 100 }) < juce::jlimit(0, 100, pct); }
}

// ===== makeHiHatPattern =====
juce::MidiMessageSequence BoomAudioProcessor::makeHiHatPattern(const juce::String& style,
    int tsNum, int tsDen, int bars,
    bool allowTriplets, bool allowDotted,
    int seed) const
{
    // RNG (deterministic if seed provided)
    juce::Random rng(seed == -1 ? (int)juce::Time::getMillisecondCounter() : seed);
    juce::MidiMessageSequence seq;

    // basic steps and bounds
    const int stepsPerBar = stepsPerBarFromTS(tsNum, tsDen); // your helper - 16th steps base
    const int clampedBars = juce::jlimit(1, 8, bars);
    const int totalSteps = stepsPerBar * clampedBars;

    // ---- SMALL LOCAL SEED BANK (16-step templates) ----
    // If you already inserted the full 300-seed bank earlier, remove or comment out this localHatSeeds
    // and instead use your earlier 'hatSeeds' vector to avoid duplication.
    static const std::vector<std::array<int, 16>> localHatSeeds = {
        {1,0,1,0, 1,0,1,0,  1,0,1,0, 1,0,1,0},
        {1,0,0,0, 1,0,1,0,  1,0,0,0, 1,0,1,0},
        {1,0,1,0, 0,1,0,0,  1,0,1,0, 0,1,0,0},
        {1,0,0,0, 0,1,0,1,  1,0,0,0, 0,1,0,1},
        {1,0,0,0, 1,0,0,0,  1,0,1,0, 0,0,1,0},
        {1,0,0,1, 0,0,1,0,  1,0,0,1, 0,0,1,0},
        {1,0,0,0, 0,0,1,1,  0,1,0,0, 1,0,0,0},
        {1,0,1,0, 0,0,1,0,  1,0,1,0, 0,0,1,0},
        {1,0,0,0, 0,0,0,1,  0,1,0,0, 1,0,0,1},
        {1,0,0,0, 0,1,0,0,  1,0,0,0, 0,1,0,0},

        // 11-20
        {1,0,1,0, 1,0,0,1,  1,0,1,0, 0,1,0,0},
        {1,0,0,1, 0,1,0,1,  1,0,0,1, 0,1,0,1},
        {1,0,0,0, 1,0,1,1,  0,1,0,0, 1,0,1,0},
        {1,0,1,0, 0,1,0,1,  0,1,0,0, 1,0,0,0},
        {1,0,0,0, 0,0,1,0,  1,0,0,1, 0,0,1,0},
        {1,0,0,1, 0,0,0,0,  1,0,0,1, 0,0,0,0},
        {1,0,1,0, 0,0,1,0,  0,0,1,0, 1,0,0,0},
        {1,0,0,0, 0,0,1,0,  0,1,0,1, 1,0,0,0},
        {1,0,0,1, 0,0,1,0,  1,0,1,0, 0,0,1,0},
        {1,0,1,0, 0,0,0,1,  0,1,0,0, 1,0,0,0},

        // 21-30
        {1,0,0,0, 0,1,1,0,  0,0,1,0, 1,0,0,0},
        {1,0,0,1, 0,1,0,0,  1,0,0,1, 0,0,0,1},
        {1,0,1,0, 0,1,0,0,  0,1,1,0, 0,0,1,0},
        {1,0,0,0, 1,0,0,1,  0,1,0,0, 1,0,0,1},
        {1,0,0,1, 0,0,0,1,  0,1,0,0, 1,0,1,0},
        {1,0,1,0, 1,0,0,0,  1,0,0,1, 0,0,1,0},
        {1,0,0,0, 0,1,0,1,  0,0,1,0, 1,0,1,0},
        {1,0,0,1, 0,0,1,0,  0,1,0,0, 1,0,0,0},
        {1,0,1,0, 0,0,1,1,  1,0,0,0, 0,1,0,0},
        {1,0,0,0, 0,0,0,1,  1,0,0,0, 0,0,1,0},

        // 31-40
        {1,0,1,0, 0,1,0,0,  1,0,1,0, 0,0,0,1},
        {1,0,0,0, 1,0,1,0,  0,1,0,0, 1,0,0,0},
        {1,0,0,1, 0,0,1,0,  1,0,0,0, 0,0,1,1},
        {1,0,0,0, 0,1,0,0,  0,0,1,0, 1,0,1,0},
        {1,0,0,1, 0,1,0,0,  1,0,1,0, 0,1,0,0},
        {1,0,1,0, 0,0,0,1,  1,0,0,1, 0,0,0,1},
        {1,0,0,0, 0,0,1,0,  1,0,1,0, 0,1,0,1},
        {1,0,1,0, 0,0,1,0,  1,0,0,0, 0,1,0,0},
        {1,0,0,1, 0,0,0,1,  0,1,1,0, 0,0,1,0},
        {1,0,0,0, 1,0,0,1,  0,0,1,0, 1,0,0,0},

        // 41-50
        {1,0,0,1, 0,0,1,0,  1,0,0,1, 0,0,1,0},
        {1,0,1,0, 0,1,0,0,  0,0,1,0, 1,0,0,1},
        {1,0,0,0, 0,1,0,1,  1,0,0,0, 0,0,1,0},
        {1,0,0,1, 0,0,0,0,  1,0,1,0, 0,1,0,0},
        {1,0,0,0, 0,0,1,1,  0,1,0,0, 1,0,0,1},
        {1,0,1,0, 0,0,0,1,  1,0,0,0, 0,1,0,1},
        {1,0,0,0, 1,0,0,0,  0,1,0,0, 1,0,1,0},
        {1,0,0,1, 0,1,0,0,  1,0,0,0, 0,0,1,0},
        {1,0,1,0, 0,0,1,0,  0,0,1,0, 0,1,0,1},
        {1,0,0,0, 0,1,1,0,  0,0,1,0, 1,0,0,0},

        // 51-60
        {1,0,0,1, 0,0,0,1,  0,1,0,0, 1,0,0,1},
        {1,0,1,0, 0,0,1,0,  1,0,0,0, 0,1,0,0},
        {1,0,0,0, 0,0,0,1,  1,0,0,1, 0,0,1,0},
        {1,0,0,1, 0,1,0,0,  0,0,1,0, 1,0,0,0},
        {1,0,1,0, 0,0,1,1,  0,0,1,0, 1,0,0,0},
        {1,0,0,0, 0,1,0,1,  1,0,0,0, 0,1,0,0},
        {1,0,0,1, 0,0,1,0,  1,0,1,0, 0,0,1,0},
        {1,0,1,0, 0,0,0,0,  1,0,1,0, 0,1,0,1},
        {1,0,0,0, 1,0,0,1,  0,0,1,0, 0,1,0,0},
        {1,0,0,1, 0,0,0,1,  1,0,0,0, 0,1,1,0},

        // 61-70
        {1,0,1,0, 0,1,0,0,  1,0,0,1, 0,0,1,0},
        {1,0,0,0, 0,0,1,0,  1,0,1,0, 0,0,1,0},
        {1,0,0,1, 0,0,1,0,  0,1,0,1, 0,0,0,1},
        {1,0,1,0, 0,0,0,1,  0,1,0,0, 1,0,0,0},
        {1,0,0,0, 1,0,1,0,  1,0,0,0, 0,0,1,0},
        {1,0,0,1, 0,1,0,0,  1,0,0,1, 0,1,0,0},
        {1,0,1,0, 0,0,1,0,  1,0,0,0, 0,1,1,0},
        {1,0,0,0, 0,1,0,1,  1,0,0,0, 0,0,1,0},
        {1,0,0,1, 0,0,1,0,  0,1,0,0, 1,0,0,1},
        {1,0,1,0, 0,0,0,0,  1,0,1,0, 0,0,1,0},

        // 71-80
        {1,0,0,0, 0,0,1,0,  1,0,0,1, 0,1,0,0},
        {1,0,0,1, 0,1,0,0,  0,0,1,0, 1,0,0,1},
        {1,0,1,0, 0,0,1,0,  1,0,0,1, 0,0,0,0},
        {1,0,0,0, 1,0,0,1,  0,1,0,0, 1,0,0,0},
        {1,0,0,1, 0,0,0,1,  1,0,0,1, 0,0,1,0},
        {1,0,1,0, 0,1,0,1,  0,0,1,0, 1,0,0,0},
        {1,0,0,0, 0,1,0,0,  1,0,1,0, 0,0,1,1},
        {1,0,0,1, 0,0,1,0,  1,0,0,0, 0,1,0,0},
        {1,0,1,0, 0,0,0,1,  1,0,1,0, 0,1,0,0},
        {1,0,0,0, 0,0,1,1,  0,1,0,0, 1,0,1,0},

        // 81-90
        {1,0,1,0, 0,0,1,0,  1,0,0,1, 0,0,1,0},
        {1,0,0,0, 1,0,0,0,  0,1,0,1, 1,0,0,0},
        {1,0,0,1, 0,0,1,0,  0,1,0,0, 1,0,0,1},
        {1,0,1,0, 0,0,0,1,  0,0,1,0, 1,0,0,0},
        {1,0,0,0, 0,1,0,1,  1,0,0,0, 0,1,0,0},
        {1,0,0,1, 0,1,0,0,  1,0,0,1, 0,0,1,0},
        {1,0,1,0, 0,0,1,0,  0,1,1,0, 1,0,0,0},
        {1,0,0,0, 1,0,0,1,  0,0,1,0, 1,0,0,0},
        {1,0,0,1, 0,0,0,0,  1,0,1,0, 0,0,1,0},
        {1,0,1,0, 0,1,0,0,  1,0,0,0, 0,0,1,0},

        // 91-100
        {1,0,0,0, 0,0,1,0,  1,0,1,0, 0,1,0,0},
        {1,0,0,1, 0,0,0,1,  0,1,0,0, 1,0,0,1},
        {1,0,1,0, 0,0,1,0,  1,0,0,1, 0,1,0,0},
        {1,0,0,0, 1,0,0,1,  0,0,1,0, 1,0,0,0},
        {1,0,0,1, 0,1,0,0,  1,0,0,0, 0,0,1,1},
        {1,0,1,0, 0,0,0,1,  1,0,0,1, 0,0,1,0},
        {1,0,0,0, 0,1,0,1,  0,0,1,0, 1,0,1,0},
        {1,0,1,0, 0,0,1,0,  0,1,0,0, 1,0,0,1},
        {1,0,0,1, 0,0,0,0,  1,0,1,0, 0,1,0,0},
        {1,0,0,0, 0,0,1,1,  1,0,0,1, 0,0,1,0},

        // 101-110
{1,0,0,1, 0,0,1,0,  1,0,0,0, 0,1,0,0},
{1,0,1,0, 0,0,0,1,  1,0,0,1, 0,0,1,0},
{1,0,0,0, 0,1,0,0,  1,0,1,0, 0,0,1,0},
{1,0,0,1, 0,0,1,0,  0,1,0,1, 0,0,0,0},
{1,0,0,0, 1,0,0,1,  0,0,1,0, 1,0,0,0},
{1,0,1,0, 0,1,0,0,  1,0,0,0, 0,0,1,0},
{1,0,0,0, 0,0,1,1,  0,1,0,0, 1,0,0,1},
{1,0,0,1, 0,0,0,0,  1,0,1,0, 0,1,0,0},
{1,0,1,0, 0,0,1,0,  0,0,1,0, 1,0,0,0},
{1,0,0,0, 0,1,0,1,  1,0,0,0, 0,0,1,0},

// 111-120
{1,0,0,1, 0,0,1,0,  1,0,0,1, 0,0,0,1},
{1,0,1,0, 0,0,0,0,  1,0,0,1, 0,1,0,0},
{1,0,0,0, 1,0,0,0,  1,0,1,0, 0,0,1,1},
{1,0,0,1, 0,1,0,0,  0,1,0,0, 1,0,0,0},
{1,0,1,0, 0,0,1,1,  0,0,1,0, 1,0,0,0},
{1,0,0,0, 0,0,1,0,  0,1,0,0, 1,0,1,0},
{1,0,0,1, 0,0,0,1,  0,0,1,0, 1,0,0,0},
{1,0,1,0, 0,1,0,0,  1,0,0,1, 0,0,0,1},
{1,0,0,0, 0,0,1,0,  1,0,0,0, 0,1,0,1},
{1,0,0,1, 0,0,1,0,  0,1,0,0, 1,0,1,0},

// 121-130
{1,0,1,0, 0,0,0,1,  0,1,0,0, 1,0,0,0},
{1,0,0,0, 0,1,0,0,  1,0,1,0, 0,0,1,0},
{1,0,0,1, 0,0,1,0,  1,0,0,0, 0,1,0,0},
{1,0,1,0, 0,0,1,0,  0,0,1,0, 1,0,0,1},
{1,0,0,0, 1,0,0,1,  0,1,0,0, 0,0,1,0},
{1,0,0,1, 0,1,0,0,  1,0,0,1, 0,0,0,0},
{1,0,1,0, 0,0,0,1,  1,0,0,0, 0,1,0,0},
{1,0,0,0, 0,0,1,1,  0,1,0,0, 1,0,0,1},
{1,0,1,0, 0,0,1,0,  1,0,0,0, 0,0,1,0},
{1,0,0,1, 0,0,0,1,  0,1,1,0, 0,0,1,0},

// 131-140
{1,0,0,0, 1,0,0,0,  0,1,0,1, 1,0,0,0},
{1,0,0,1, 0,0,1,0,  0,0,1,0, 1,0,0,1},
{1,0,1,0, 0,1,0,0,  1,0,0,0, 0,0,1,0},
{1,0,0,0, 0,1,1,0,  0,0,1,0, 1,0,0,0},
{1,0,1,0, 0,0,1,0,  0,1,0,1, 1,0,0,0},
{1,0,0,0, 0,0,1,0,  1,0,1,0, 0,1,0,0},
{1,0,0,1, 0,0,0,0,  1,0,1,0, 0,0,1,0},
{1,0,1,0, 0,0,0,1,  0,0,1,0, 1,0,0,1},
{1,0,0,0, 0,1,0,1,  0,0,1,0, 1,0,0,0},
{1,0,0,1, 0,0,1,0,  1,0,0,1, 0,1,0,0},

// 141-150
{1,0,1,0, 0,0,1,0,  0,0,1,0, 1,0,1,0},
{1,0,0,0, 1,0,0,1,  0,0,1,0, 0,1,0,0},
{1,0,0,1, 0,1,0,0,  1,0,0,0, 0,0,1,0},
{1,0,1,0, 0,0,1,0,  1,0,0,1, 0,0,0,0},
{1,0,0,0, 0,1,0,0,  1,0,1,0, 0,0,1,1},
{1,0,0,1, 0,0,1,0,  0,1,0,1, 0,0,0,1},
{1,0,1,0, 0,0,0,1,  1,0,0,0, 0,1,0,0},
{1,0,0,0, 0,0,1,1,  1,0,0,0, 0,1,0,0},
{1,0,0,1, 0,0,0,0,  1,0,0,1, 0,0,1,0},
{1,0,1,0, 0,1,0,0,  0,0,1,0, 1,0,0,0},

// 151-160
{1,0,0,0, 0,1,0,1,  1,0,0,0, 0,0,1,0},
{1,0,0,1, 0,0,1,0,  0,1,0,0, 1,0,0,1},
{1,0,1,0, 0,0,0,1,  0,1,0,0, 1,0,0,0},
{1,0,0,0, 0,0,1,0,  1,0,0,1, 0,1,0,0},
{1,0,0,1, 0,1,0,0,  1,0,0,1, 0,0,0,1},
{1,0,1,0, 0,0,1,0,  1,0,1,0, 0,0,1,0},
{1,0,0,0, 1,0,0,0,  0,1,0,1, 1,0,0,0},
{1,0,0,1, 0,0,0,1,  1,0,0,0, 0,1,0,0},
{1,0,1,0, 0,1,0,0,  0,0,1,1, 1,0,0,0},
{1,0,0,0, 0,0,1,0,  1,0,1,0, 0,0,1,0},

// 161-170
{1,0,0,1, 0,0,1,0,  0,1,0,0, 1,0,0,0},
{1,0,1,0, 0,0,0,0,  1,0,0,1, 0,1,0,0},
{1,0,0,0, 0,1,0,0,  1,0,0,1, 0,0,1,0},
{1,0,0,1, 0,0,1,0,  1,0,1,0, 0,0,0,1},
{1,0,1,0, 0,0,1,1,  0,0,1,0, 1,0,0,0},
{1,0,0,0, 1,0,0,1,  0,0,1,0, 0,1,0,0},
{1,0,0,1, 0,1,0,0,  1,0,0,0, 0,1,0,1},
{1,0,1,0, 0,0,0,1,  0,0,1,0, 1,0,0,0},
{1,0,0,0, 0,1,1,0,  0,0,1,0, 1,0,0,1},
{1,0,0,1, 0,0,0,1,  1,0,0,1, 0,0,1,0},

// 171-180
{1,0,1,0, 0,0,1,0,  1,0,0,0, 0,1,0,0},
{1,0,0,0, 1,0,0,1,  0,1,0,0, 1,0,0,0},
{1,0,0,1, 0,0,1,0,  0,0,1,0, 1,0,0,1},
{1,0,1,0, 0,1,0,0,  1,0,0,1, 0,0,0,0},
{1,0,0,0, 0,0,1,1,  1,0,0,0, 0,1,0,0},
{1,0,0,1, 0,0,0,1,  0,1,1,0, 1,0,0,0},
{1,0,1,0, 0,0,0,0,  1,0,1,0, 0,1,0,1},
{1,0,0,0, 1,0,0,0,  0,0,1,0, 1,0,0,1},
{1,0,0,1, 0,1,0,0,  1,0,0,1, 0,0,1,0},
{1,0,1,0, 0,0,1,0,  0,1,0,0, 1,0,0,0},

// 181-190
{1,0,0,0, 0,1,0,1,  1,0,0,0, 0,0,1,0},
{1,0,0,1, 0,0,1,0,  1,0,1,0, 0,0,1,0},
{1,0,1,0, 0,0,0,1,  0,1,0,0, 1,0,1,0},
{1,0,0,0, 0,0,1,1,  0,1,0,0, 1,0,0,0},
{1,0,0,1, 0,1,0,0,  1,0,0,1, 0,0,0,1},
{1,0,1,0, 0,0,1,0,  1,0,0,0, 0,1,0,0},
{1,0,0,0, 1,0,0,1,  0,0,1,0, 1,0,0,1},
{1,0,0,1, 0,0,0,0,  1,0,1,0, 0,0,1,0},
{1,0,1,0, 0,1,0,0,  1,0,0,0, 0,0,1,1},
{1,0,0,0, 0,0,1,0,  1,0,0,1, 0,1,0,0},

// 191-200
{1,0,0,1, 0,0,1,0,  0,1,0,1, 0,0,0,1},
{1,0,1,0, 0,0,0,1,  1,0,0,0, 0,1,0,0},
{1,0,0,0, 1,0,1,0,  0,0,1,0, 1,0,0,0},
{1,0,0,1, 0,1,0,0,  1,0,0,0, 0,0,1,0},
{1,0,1,0, 0,0,1,0,  1,0,1,0, 0,0,0,1},
{1,0,0,0, 0,0,1,1,  1,0,0,1, 0,0,1,0},
{1,0,0,1, 0,0,0,1,  1,0,0,0, 0,1,0,0},
{1,0,1,0, 0,1,0,0,  0,0,1,0, 1,0,0,1},
{1,0,0,0, 0,0,0,1,  1,0,1,0, 0,1,0,0},
{1,0,0,1, 0,0,1,0,  1,0,0,0, 0,0,1,0},

        // 201-210
{1,0,0,1, 0,0,1,0,  1,0,0,1, 0,0,1,0},
{1,0,1,0, 0,0,1,0,  0,1,0,0, 1,0,0,0},
{1,0,0,0, 0,1,0,1,  1,0,0,0, 0,0,1,0},
{1,0,0,1, 0,0,0,1,  0,1,0,0, 1,0,0,1},
{1,0,1,0, 0,0,0,0,  1,0,1,0, 0,1,0,0},
{1,0,0,0, 0,0,1,1,  0,1,0,0, 1,0,0,0},
{1,0,0,1, 0,1,0,0,  1,0,0,1, 0,0,0,1},
{1,0,1,0, 0,0,1,0,  1,0,0,0, 0,1,0,0},
{1,0,0,0, 1,0,0,1,  0,0,1,0, 1,0,0,0},
{1,0,0,1, 0,0,1,0,  0,1,1,0, 0,0,1,0},

// 211-220
{1,0,1,0, 0,1,0,0,  1,0,0,1, 0,0,1,0},
{1,0,0,0, 0,0,1,0,  1,0,0,1, 0,1,0,0},
{1,0,0,1, 0,0,0,1,  1,0,0,0, 0,1,0,0},
{1,0,1,0, 0,0,0,1,  0,1,0,0, 1,0,0,1},
{1,0,0,0, 0,1,0,1,  0,0,0,1, 1,0,0,0},
{1,0,0,1, 0,0,1,0,  1,0,1,0, 0,0,1,0},
{1,0,1,0, 0,0,1,1,  1,0,0,0, 0,1,0,0},
{1,0,0,0, 0,0,0,1,  1,0,0,1, 0,0,1,0},
{1,0,0,1, 0,1,0,0,  0,0,1,0, 1,0,0,1},
{1,0,1,0, 0,0,0,0,  1,0,0,0, 0,1,0,1},

// 221-230
{1,0,0,0, 1,0,1,0,  0,1,0,0, 1,0,0,0},
{1,0,0,1, 0,0,0,1,  0,0,1,0, 1,0,0,0},
{1,0,1,0, 0,0,1,0,  0,1,0,1, 0,0,0,1},
{1,0,0,0, 0,1,0,0,  1,0,1,0, 0,0,1,0},
{1,0,0,1, 0,0,1,0,  1,0,0,0, 0,1,1,0},
{1,0,1,0, 0,1,0,0,  1,0,0,1, 0,0,0,0},
{1,0,0,0, 0,0,1,0,  1,0,0,1, 0,0,1,0},
{1,0,0,1, 0,0,0,0,  1,0,1,0, 0,1,0,0},
{1,0,1,0, 0,0,1,0,  1,0,0,0, 0,1,0,1},
{1,0,0,0, 0,1,1,0,  1,0,0,0, 0,0,1,0},

// 231-240
{1,0,0,1, 0,0,1,0,  0,1,0,0, 1,0,1,0},
{1,0,1,0, 0,0,0,1,  0,1,0,0, 1,0,0,0},
{1,0,0,0, 1,0,0,1,  0,0,1,0, 1,0,0,1},
{1,0,0,1, 0,1,0,0,  1,0,0,1, 0,0,1,0},
{1,0,1,0, 0,0,1,0,  1,0,1,0, 0,0,0,1},
{1,0,0,0, 0,1,0,0,  1,0,0,1, 0,0,1,0},
{1,0,0,1, 0,0,0,1,  0,1,1,0, 0,0,1,0},
{1,0,1,0, 0,1,0,0,  1,0,0,0, 0,0,1,1},
{1,0,0,0, 0,0,1,0,  1,0,0,1, 0,1,0,0},
{1,0,0,1, 0,0,1,0,  1,0,0,0, 0,1,0,0},

// 241-250
{1,0,1,0, 0,0,1,0,  0,1,0,0, 1,0,0,0},
{1,0,0,0, 1,0,0,1,  0,0,1,0, 0,1,0,0},
{1,0,0,1, 0,0,1,0,  0,1,0,0, 1,0,1,0},
{1,0,1,0, 0,0,0,1,  1,0,0,0, 0,1,0,0},
{1,0,0,0, 0,1,0,1,  1,0,0,1, 0,0,0,0},
{1,0,0,1, 0,0,0,0,  1,0,0,1, 0,0,1,0},
{1,0,1,0, 0,1,0,0,  1,0,1,0, 0,0,1,0},
{1,0,0,0, 0,0,1,1,  1,0,0,0, 0,1,0,0},
{1,0,0,1, 0,0,1,0,  1,0,0,0, 0,0,1,1},
{1,0,1,0, 0,0,0,0,  1,0,0,1, 0,1,0,0},

// 251-260
{1,0,0,0, 0,1,0,0,  1,0,1,0, 0,0,1,0},
{1,0,0,1, 0,0,1,0,  0,1,0,1, 0,0,0,1},
{1,0,1,0, 0,0,1,0,  1,0,0,0, 0,1,0,0},
{1,0,0,0, 1,0,0,1,  0,0,1,0, 1,0,0,0},
{1,0,0,1, 0,1,0,0,  1,0,0,1, 0,0,0,1},
{1,0,1,0, 0,0,0,1,  0,1,0,0, 1,0,0,0},
{1,0,0,0, 0,0,1,1,  0,1,0,0, 1,0,0,0},
{1,0,0,1, 0,0,0,0,  1,0,1,0, 0,1,0,0},
{1,0,1,0, 0,0,1,0,  0,1,0,0, 1,0,1,0},
{1,0,0,0, 0,1,1,0,  0,0,1,0, 1,0,0,0},

// 261-270
{1,0,0,1, 0,1,0,0,  1,0,0,0, 0,0,1,0},
{1,0,1,0, 0,0,1,0,  1,0,0,1, 0,0,0,1},
{1,0,0,0, 0,1,0,1,  1,0,0,0, 0,0,1,0},
{1,0,0,1, 0,0,1,0,  1,0,1,0, 0,1,0,0},
{1,0,1,0, 0,0,0,1,  0,1,0,0, 1,0,0,1},
{1,0,0,0, 1,0,0,0,  0,1,0,1, 1,0,0,0},
{1,0,0,1, 0,0,0,1,  1,0,0,0, 0,1,0,0},
{1,0,1,0, 0,1,0,0,  0,0,1,0, 1,0,0,0},
{1,0,0,0, 0,0,1,0,  1,0,0,1, 0,0,1,0},
{1,0,0,1, 0,0,1,0,  0,1,0,0, 1,0,0,1},

// 271-280
{1,0,1,0, 0,0,1,1,  0,0,1,0, 1,0,0,0},
{1,0,0,0, 1,0,0,1,  0,1,0,0, 1,0,0,0},
{1,0,0,1, 0,0,0,0,  1,0,1,0, 0,1,0,0},
{1,0,1,0, 0,1,0,0,  1,0,0,1, 0,0,0,1},
{1,0,0,0, 0,0,1,0,  1,0,1,0, 0,1,0,0},
{1,0,0,1, 0,0,1,0,  1,0,0,0, 0,1,0,1},
{1,0,1,0, 0,0,0,1,  0,1,0,0, 1,0,0,0},
{1,0,0,0, 0,1,0,1,  1,0,0,0, 0,0,1,0},
{1,0,0,1, 0,0,1,0,  0,1,0,1, 1,0,0,0},
{1,0,1,0, 0,0,1,0,  1,0,0,0, 0,0,1,1},

// 281-290
{1,0,0,0, 1,0,0,0,  0,1,0,1, 0,0,1,0},
{1,0,0,1, 0,1,0,0,  1,0,0,1, 0,0,0,1},
{1,0,1,0, 0,0,1,0,  1,0,1,0, 0,0,0,0},
{1,0,0,0, 0,1,1,0,  0,0,1,0, 1,0,0,0},
{1,0,0,1, 0,0,0,1,  0,1,0,0, 1,0,1,0},
{1,0,1,0, 0,0,1,0,  0,1,0,0, 1,0,0,1},
{1,0,0,0, 1,0,0,1,  0,0,1,0, 1,0,0,0},
{1,0,0,1, 0,0,1,1,  1,0,0,0, 0,1,0,0},
{1,0,1,0, 0,0,0,0,  1,0,0,1, 0,1,0,0},
{1,0,0,0, 0,0,1,0,  1,0,0,1, 0,0,1,0},

// 291-300
{1,0,0,1, 0,0,0,1,  0,1,0,0, 1,0,0,0},
{1,0,1,0, 0,1,0,0,  1,0,0,1, 0,0,0,1},
{1,0,0,0, 0,0,1,1,  1,0,0,0, 0,1,0,0},
{1,0,0,1, 0,0,1,0,  1,0,1,0, 0,0,1,0},
{1,0,1,0, 0,0,0,1,  1,0,0,0, 0,1,0,0},
{1,0,0,0, 1,0,0,1,  0,0,1,0, 1,0,0,1},
{1,0,0,1, 0,1,0,0,  1,0,0,0, 0,0,1,0},
{1,0,1,0, 0,0,1,0,  0,1,0,0, 1,0,0,0},
{1,0,0,0, 0,1,0,1,  1,0,0,0, 0,0,1,0},
{1,0,0,1, 0,0,1,0,  1,0,0,1, 0,0,0,1}
        // (add more here if you want; the full 300 seeds you already pasted can replace this block)
    };
    std::vector<juce::String> seedNames;
    // reserve from the local seed bank (fallback). 'hatSeeds' was never defined in this TU.
    seedNames.reserve(localHatSeeds.size());
    for (int i = 1; i <= 100; ++i)
        seedNames.push_back("hatSeed_" + juce::String(i).paddedLeft('0', 3));

    // pick a seed index using RNG (deterministic by seed param)

    // pick a seed index using RNG (deterministic by seed param)
    const int chosenSeedIdx = rng.nextInt({ (int)localHatSeeds.size() });
    const auto& baseTemplate = localHatSeeds[chosenSeedIdx];

    // debug: which seed was chosen
    DBG("makeHiHatPattern selected seed: " + seedNames[chosenSeedIdx] + " (idx=" + juce::String(chosenSeedIdx) + ")");

    seedNames.reserve(100);

    seedNames.push_back("hatSeed_0");
    seedNames.push_back("hatSeed_1");
    seedNames.push_back("hatSeed_2");
    seedNames.push_back("hatSeed_3");
    seedNames.push_back("hatSeed_4");
    seedNames.push_back("hatSeed_5");
    seedNames.push_back("hatSeed_6");
    seedNames.push_back("hatSeed_7");
    seedNames.push_back("hatSeed_8");
    seedNames.push_back("hatSeed_9");

    seedNames.push_back("hatSeed_10");
    seedNames.push_back("hatSeed_11");
    seedNames.push_back("hatSeed_12");
    seedNames.push_back("hatSeed_13");
    seedNames.push_back("hatSeed_14");
    seedNames.push_back("hatSeed_15");
    seedNames.push_back("hatSeed_16");
    seedNames.push_back("hatSeed_17");
    seedNames.push_back("hatSeed_18");
    seedNames.push_back("hatSeed_19");

    seedNames.push_back("hatSeed_20");
    seedNames.push_back("hatSeed_21");
    seedNames.push_back("hatSeed_22");
    seedNames.push_back("hatSeed_23");
    seedNames.push_back("hatSeed_24");
    seedNames.push_back("hatSeed_25");
    seedNames.push_back("hatSeed_26");
    seedNames.push_back("hatSeed_27");
    seedNames.push_back("hatSeed_28");
    seedNames.push_back("hatSeed_29");

    seedNames.push_back("hatSeed_30");
    seedNames.push_back("hatSeed_31");
    seedNames.push_back("hatSeed_32");
    seedNames.push_back("hatSeed_33");
    seedNames.push_back("hatSeed_34");
    seedNames.push_back("hatSeed_35");
    seedNames.push_back("hatSeed_36");
    seedNames.push_back("hatSeed_37");
    seedNames.push_back("hatSeed_38");
    seedNames.push_back("hatSeed_39");

    seedNames.push_back("hatSeed_40");
    seedNames.push_back("hatSeed_41");
    seedNames.push_back("hatSeed_42");
    seedNames.push_back("hatSeed_43");
    seedNames.push_back("hatSeed_44");
    seedNames.push_back("hatSeed_45");
    seedNames.push_back("hatSeed_46");
    seedNames.push_back("hatSeed_47");
    seedNames.push_back("hatSeed_48");
    seedNames.push_back("hatSeed_49");

    seedNames.push_back("hatSeed_50");
    seedNames.push_back("hatSeed_51");
    seedNames.push_back("hatSeed_52");
    seedNames.push_back("hatSeed_53");
    seedNames.push_back("hatSeed_54");
    seedNames.push_back("hatSeed_55");
    seedNames.push_back("hatSeed_56");
    seedNames.push_back("hatSeed_57");
    seedNames.push_back("hatSeed_58");
    seedNames.push_back("hatSeed_59");

    seedNames.push_back("hatSeed_60");
    seedNames.push_back("hatSeed_61");
    seedNames.push_back("hatSeed_62");
    seedNames.push_back("hatSeed_63");
    seedNames.push_back("hatSeed_64");
    seedNames.push_back("hatSeed_65");
    seedNames.push_back("hatSeed_66");
    seedNames.push_back("hatSeed_67");
    seedNames.push_back("hatSeed_68");
    seedNames.push_back("hatSeed_69");

    seedNames.push_back("hatSeed_70");
    seedNames.push_back("hatSeed_71");
    seedNames.push_back("hatSeed_72");
    seedNames.push_back("hatSeed_73");
    seedNames.push_back("hatSeed_74");
    seedNames.push_back("hatSeed_75");
    seedNames.push_back("hatSeed_76");
    seedNames.push_back("hatSeed_77");
    seedNames.push_back("hatSeed_78");
    seedNames.push_back("hatSeed_79");

    seedNames.push_back("hatSeed_80");
    seedNames.push_back("hatSeed_81");
    seedNames.push_back("hatSeed_82");
    seedNames.push_back("hatSeed_83");
    seedNames.push_back("hatSeed_84");
    seedNames.push_back("hatSeed_85");
    seedNames.push_back("hatSeed_86");
    seedNames.push_back("hatSeed_87");
    seedNames.push_back("hatSeed_88");
    seedNames.push_back("hatSeed_89");

    seedNames.push_back("hatSeed_90");
    seedNames.push_back("hatSeed_91");
    seedNames.push_back("hatSeed_92");
    seedNames.push_back("hatSeed_93");
    seedNames.push_back("hatSeed_94");
    seedNames.push_back("hatSeed_95");
    seedNames.push_back("hatSeed_96");
    seedNames.push_back("hatSeed_97");
    seedNames.push_back("hatSeed_98");
    seedNames.push_back("hatSeed_99");
    seedNames.push_back("hatSeed_100");

    seedNames.push_back("hatSeed_101"); seedNames.push_back("hatSeed_102"); seedNames.push_back("hatSeed_103");
    seedNames.push_back("hatSeed_104"); seedNames.push_back("hatSeed_105"); seedNames.push_back("hatSeed_106");
    seedNames.push_back("hatSeed_107"); seedNames.push_back("hatSeed_108"); seedNames.push_back("hatSeed_109");
    seedNames.push_back("hatSeed_110"); seedNames.push_back("hatSeed_111"); seedNames.push_back("hatSeed_112");
    seedNames.push_back("hatSeed_113"); seedNames.push_back("hatSeed_114"); seedNames.push_back("hatSeed_115");
    seedNames.push_back("hatSeed_116"); seedNames.push_back("hatSeed_117"); seedNames.push_back("hatSeed_118");
    seedNames.push_back("hatSeed_119"); seedNames.push_back("hatSeed_120"); seedNames.push_back("hatSeed_121");
    seedNames.push_back("hatSeed_122"); seedNames.push_back("hatSeed_123"); seedNames.push_back("hatSeed_124");
    seedNames.push_back("hatSeed_125"); seedNames.push_back("hatSeed_126"); seedNames.push_back("hatSeed_127");
    seedNames.push_back("hatSeed_128"); seedNames.push_back("hatSeed_129"); seedNames.push_back("hatSeed_130");
    seedNames.push_back("hatSeed_131"); seedNames.push_back("hatSeed_132"); seedNames.push_back("hatSeed_133");
    seedNames.push_back("hatSeed_134"); seedNames.push_back("hatSeed_135"); seedNames.push_back("hatSeed_136");
    seedNames.push_back("hatSeed_137"); seedNames.push_back("hatSeed_138"); seedNames.push_back("hatSeed_139");
    seedNames.push_back("hatSeed_140"); seedNames.push_back("hatSeed_141"); seedNames.push_back("hatSeed_142");
    seedNames.push_back("hatSeed_143"); seedNames.push_back("hatSeed_144"); seedNames.push_back("hatSeed_145");
    seedNames.push_back("hatSeed_146"); seedNames.push_back("hatSeed_147"); seedNames.push_back("hatSeed_148");
    seedNames.push_back("hatSeed_149"); seedNames.push_back("hatSeed_150"); seedNames.push_back("hatSeed_151");
    seedNames.push_back("hatSeed_152"); seedNames.push_back("hatSeed_153"); seedNames.push_back("hatSeed_154");
    seedNames.push_back("hatSeed_155"); seedNames.push_back("hatSeed_156"); seedNames.push_back("hatSeed_157");
    seedNames.push_back("hatSeed_158"); seedNames.push_back("hatSeed_159"); seedNames.push_back("hatSeed_160");
    seedNames.push_back("hatSeed_161"); seedNames.push_back("hatSeed_162"); seedNames.push_back("hatSeed_163");
    seedNames.push_back("hatSeed_164"); seedNames.push_back("hatSeed_165"); seedNames.push_back("hatSeed_166");
    seedNames.push_back("hatSeed_167"); seedNames.push_back("hatSeed_168"); seedNames.push_back("hatSeed_169");
    seedNames.push_back("hatSeed_170"); seedNames.push_back("hatSeed_171"); seedNames.push_back("hatSeed_172");
    seedNames.push_back("hatSeed_173"); seedNames.push_back("hatSeed_174"); seedNames.push_back("hatSeed_175");
    seedNames.push_back("hatSeed_176"); seedNames.push_back("hatSeed_177"); seedNames.push_back("hatSeed_178");
    seedNames.push_back("hatSeed_179"); seedNames.push_back("hatSeed_180"); seedNames.push_back("hatSeed_181");
    seedNames.push_back("hatSeed_182"); seedNames.push_back("hatSeed_183"); seedNames.push_back("hatSeed_184");
    seedNames.push_back("hatSeed_185"); seedNames.push_back("hatSeed_186"); seedNames.push_back("hatSeed_187");
    seedNames.push_back("hatSeed_188"); seedNames.push_back("hatSeed_189"); seedNames.push_back("hatSeed_190");
    seedNames.push_back("hatSeed_191"); seedNames.push_back("hatSeed_192"); seedNames.push_back("hatSeed_193");
    seedNames.push_back("hatSeed_194"); seedNames.push_back("hatSeed_195"); seedNames.push_back("hatSeed_196");
    seedNames.push_back("hatSeed_197"); seedNames.push_back("hatSeed_198"); seedNames.push_back("hatSeed_199");
    seedNames.push_back("hatSeed_200");
    seedNames.push_back("hatSeed_201"); seedNames.push_back("hatSeed_202"); seedNames.push_back("hatSeed_203");
    seedNames.push_back("hatSeed_204"); seedNames.push_back("hatSeed_205"); seedNames.push_back("hatSeed_206");
    seedNames.push_back("hatSeed_207"); seedNames.push_back("hatSeed_208"); seedNames.push_back("hatSeed_209");
    seedNames.push_back("hatSeed_210"); seedNames.push_back("hatSeed_211"); seedNames.push_back("hatSeed_212");
    seedNames.push_back("hatSeed_213"); seedNames.push_back("hatSeed_214"); seedNames.push_back("hatSeed_215");
    seedNames.push_back("hatSeed_216"); seedNames.push_back("hatSeed_217"); seedNames.push_back("hatSeed_218");
    seedNames.push_back("hatSeed_219"); seedNames.push_back("hatSeed_220"); seedNames.push_back("hatSeed_221");
    seedNames.push_back("hatSeed_222"); seedNames.push_back("hatSeed_223"); seedNames.push_back("hatSeed_224");
    seedNames.push_back("hatSeed_225"); seedNames.push_back("hatSeed_226"); seedNames.push_back("hatSeed_227");
    seedNames.push_back("hatSeed_228"); seedNames.push_back("hatSeed_229"); seedNames.push_back("hatSeed_230");
    seedNames.push_back("hatSeed_231"); seedNames.push_back("hatSeed_232"); seedNames.push_back("hatSeed_233");
    seedNames.push_back("hatSeed_234"); seedNames.push_back("hatSeed_235"); seedNames.push_back("hatSeed_236");
    seedNames.push_back("hatSeed_237"); seedNames.push_back("hatSeed_238"); seedNames.push_back("hatSeed_239");
    seedNames.push_back("hatSeed_240"); seedNames.push_back("hatSeed_241"); seedNames.push_back("hatSeed_242");
    seedNames.push_back("hatSeed_243"); seedNames.push_back("hatSeed_244"); seedNames.push_back("hatSeed_245");
    seedNames.push_back("hatSeed_246"); seedNames.push_back("hatSeed_247"); seedNames.push_back("hatSeed_248");
    seedNames.push_back("hatSeed_249"); seedNames.push_back("hatSeed_250"); seedNames.push_back("hatSeed_251");
    seedNames.push_back("hatSeed_252"); seedNames.push_back("hatSeed_253"); seedNames.push_back("hatSeed_254");
    seedNames.push_back("hatSeed_255"); seedNames.push_back("hatSeed_256"); seedNames.push_back("hatSeed_257");
    seedNames.push_back("hatSeed_258"); seedNames.push_back("hatSeed_259"); seedNames.push_back("hatSeed_260");
    seedNames.push_back("hatSeed_261"); seedNames.push_back("hatSeed_262"); seedNames.push_back("hatSeed_263");
    seedNames.push_back("hatSeed_264"); seedNames.push_back("hatSeed_265"); seedNames.push_back("hatSeed_266");
    seedNames.push_back("hatSeed_267"); seedNames.push_back("hatSeed_268"); seedNames.push_back("hatSeed_269");
    seedNames.push_back("hatSeed_270"); seedNames.push_back("hatSeed_271"); seedNames.push_back("hatSeed_272");
    seedNames.push_back("hatSeed_273"); seedNames.push_back("hatSeed_274"); seedNames.push_back("hatSeed_275");
    seedNames.push_back("hatSeed_276"); seedNames.push_back("hatSeed_277"); seedNames.push_back("hatSeed_278");
    seedNames.push_back("hatSeed_279"); seedNames.push_back("hatSeed_280"); seedNames.push_back("hatSeed_281");
    seedNames.push_back("hatSeed_282"); seedNames.push_back("hatSeed_283"); seedNames.push_back("hatSeed_284");
    seedNames.push_back("hatSeed_285"); seedNames.push_back("hatSeed_286"); seedNames.push_back("hatSeed_287");
    seedNames.push_back("hatSeed_288"); seedNames.push_back("hatSeed_289"); seedNames.push_back("hatSeed_290");
    seedNames.push_back("hatSeed_291"); seedNames.push_back("hatSeed_292"); seedNames.push_back("hatSeed_293");
    seedNames.push_back("hatSeed_294"); seedNames.push_back("hatSeed_295"); seedNames.push_back("hatSeed_296");
    seedNames.push_back("hatSeed_297"); seedNames.push_back("hatSeed_298"); seedNames.push_back("hatSeed_299");
    seedNames.push_back("hatSeed_300");


    // pick which seed bank to use: prefer a global 'hatSeeds' if you've defined one elsewhere
    const std::vector<std::array<int, 16>>* seedBankPtr = nullptr;
    static const std::vector<std::array<int, 16>> emptyBank;
    // try to reference an externally inserted hatSeeds (if you put it in the same file as a variable)
    // NOTE: we cannot check for arbitrary symbols at runtime; if you have a global/outer 'hatSeeds' variable,
    // remove localHatSeeds and instead reference that variable here directly.
    seedBankPtr = &localHatSeeds; // default to local


    // ------ helpers ------
    auto coinInt = [&](int p)->bool { return rng.nextInt({ 100 }) < juce::jlimit(0, 100, p); };
    auto chooseMajorityLambda = [&]() -> int {
        int r = rng.nextInt({ 100 });
        bool isDrill = style.equalsIgnoreCase("drill");
        if (isDrill)
        {
            if (r < 25) return 0; // 8ths
            if (r < 50) return 1; // 16ths
            if (r < 75) return 3; // 8th trip
            if (r < 95) return 4; // 16th trip
            return 5; // quarter
        }
        if (r < 35) return 0;
        if (r < 70) return 1;
        if (r < 82) return 5;
        if (r < 90) return 3;
        if (r < 96) return 4;
        return 2;
        };

    // map majority to tick sizes (your constants)
    const auto majorityToTick = [&](int majority)->int {
        switch (majority)
        {
        case 0: return kT8_;
        case 1: return kT16_;
        case 2: return kT32_;
        case 3: return kT8T_;
        case 4: return kT16T_;
        case 5: return kT4_;
        default: return kT16_;
        }
        };

    // ----- hybrid decision: style-weighted seed affinity -----
    float styleSeedAffinity = 0.5f; // default neutral
    juce::String s = style.toLowerCase();

    if (s == "wxstie") styleSeedAffinity = 0.90f;
    else if (s == "trap") styleSeedAffinity = 0.35f;
    else if (s == "drill") styleSeedAffinity = 0.30f;
    else if (s == "r&b") styleSeedAffinity = 0.55f;
    else if (s == "house") styleSeedAffinity = 0.25f;
    else if (s == "pop") styleSeedAffinity = 0.45f;
    else styleSeedAffinity = 0.50f;

    // If you have a drumStyles map with seedAffinity, you could override here.
    // e.g., if (auto it = drumStyles.find(style); it != drumStyles.end()) styleSeedAffinity = it->second.seedAffinity;

    const bool useSeeds = (!seedBankPtr->empty()) && (rng.nextFloat() < styleSeedAffinity);

    // Prepare mutable grid
    std::vector<int> stepHits(totalSteps, 0);

    // ---- populate stepHits either via seed or algorithmic fill ----
    int majority = chooseMajorityLambda();
    if (!allowTriplets && (majority == 3 || majority == 4)) majority = 1;
    const int majorityTick = majorityToTick(majority);

    if (useSeeds)
    {
        const int chosenIdx = rng.nextInt({ (int)seedBankPtr->size() });
        const auto& base = (*seedBankPtr)[chosenIdx];
        // tile the 16-step template across totalSteps
        for (int i = 0; i < totalSteps; ++i)
            stepHits[i] = base[i % 16];

        DBG("makeHiHatPattern: SEED path chosen -> idx=" + juce::String(chosenIdx));
    }
    else
    {
        // algorithmic: steady pulse with style-biased densification and microfills
        int stride = 1;
        switch (majority)
        {
        case 0: stride = 2; break; // 8th
        case 1: stride = 1; break; // 16th
        case 2: stride = 1; break; // 32nd -> we'll add denser motion below
        case 3: stride = 2; break; // 8th-trip -> treat like 8th
        case 4: stride = 1; break; // 16th-trip -> treat like 16th
        case 5: stride = 4; break; // quarter
        default: stride = 1; break;
        }

        for (int i = 0; i < totalSteps; ++i)
            if (i % stride == 0) stepHits[i] = 1;

        // denser inner fills for 32nd-feel or on random chance
        if (majority == 2 || coinInt(15))
        {
            for (int i = 0; i < totalSteps; ++i)
            {
                if (stepHits[i] == 1 && coinInt(45))
                {
                    if (i + 1 < totalSteps && coinInt(60)) stepHits[i + 1] = 1;
                    if (i + 2 < totalSteps && coinInt(30)) stepHits[i + 2] = 1;
                }
            }
        }

        // style-specific densify (trap/drill)
        if (s == "trap" || s == "drill")
        {
            for (int bar = 0; bar < clampedBars; ++bar)
            {
                int base = bar * stepsPerBar;
                int centers = juce::jlimit(1, 4, 1 + rng.nextInt({ 3 }));
                for (int c = 0; c < centers; ++c)
                {
                    int center = base + rng.nextInt({ juce::jmax(1, stepsPerBar) });
                    int run = rng.nextInt({ 2,5 });
                    for (int r = 0; r < run; ++r)
                        if (center + r < totalSteps) stepHits[center + r] = 1;
                }
            }
        }

        // occasional micro-roll
        if (coinInt(12))
        {
            int pos = rng.nextInt({ juce::jmax(1, totalSteps) });
            stepHits[pos] = 1;
            if (pos + 1 < totalSteps) stepHits[pos + 1] = 1;
            if (pos + 2 < totalSteps && coinInt(50)) stepHits[pos + 2] = 1;
        }

        // anchor at loop start sometimes
        if (!stepHits.empty() && !stepHits[0] && coinInt(70)) stepHits[0] = 1;

        DBG("makeHiHatPattern: ALGO path chosen (majority=" + juce::String(majority) + ", stride=" + juce::String(stride) + ")");
    }

    // ----- mutation & phrase-based edits -----
    // decide path: 0 steady, 1 gappy, 2 wild (we'll re-evaluate deterministically from RNG)
    int path = 0;
    {
        int r = rng.nextInt({ 100 });
        if (r < 45) path = 0;
        else if (r < 75) path = 1;
        else path = 2;
    }

    // small phrase shift sometimes
    if (rng.nextInt({ 100 }) < 14)
    {
        int shift = rng.nextBool() ? 1 : -1;
        std::vector<int> tmp(totalSteps);
        for (int i = 0; i < totalSteps; ++i)
            tmp[i] = stepHits[(i + shift + totalSteps) % totalSteps];
        stepHits.swap(tmp);
    }

    // per-bar mutations: drop/gappy/wild clusters, fills
    for (int bar = 0; bar < clampedBars; ++bar)
    {
        int baseStep = bar * stepsPerBar;
        bool gappy = (path == 1);
        bool wild = (path == 2);

        // gappy: remove some hits
        if (gappy)
        {
            for (int sidx = 0; sidx < stepsPerBar; ++sidx)
            {
                int idx = baseStep + sidx;
                if (idx < totalSteps && stepHits[idx] == 1 && coinInt(35))
                    stepHits[idx] = 0;
            }
        }

        // wild: add intermittent bursts and micro-shifts
        if (wild)
        {
            for (int sidx = 0; sidx < stepsPerBar; ++sidx)
            {
                int idx = baseStep + sidx;
                if (idx < totalSteps)
                {
                    if (stepHits[idx] == 0 && coinInt(12)) stepHits[idx] = 1;
                    if (coinInt(8) && sidx < stepsPerBar - 1)
                        std::swap(stepHits[idx], stepHits[idx + 1]);
                }
            }
        }

        // clusters: 0..2 per bar
        int clusters = rng.nextInt({ 3 });
        for (int c = 0; c < clusters; ++c)
        {
            int center = baseStep + rng.nextInt({ juce::jmax(1, stepsPerBar) });
            for (int off = -2; off <= 2; ++off)
            {
                int idx = center + off;
                if (idx >= baseStep && idx < baseStep + stepsPerBar && idx < totalSteps)
                {
                    int prob = juce::jmax(15, 70 - std::abs(off) * 20);
                    if (rng.nextInt({ 100 }) < prob) stepHits[idx] = 1;
                }
            }
        }

        // short fill at end of bar (based on wild/roll chance)
        if (wild || coinInt(20))
        {
            int pos = baseStep + stepsPerBar - 1;
            int fills = rng.nextInt({ 1,4 });
            for (int f = 0; f < fills; ++f)
            {
                int idx = pos - f;
                if (idx >= baseStep && idx < totalSteps) stepHits[idx] = 1;
            }
        }
    }

    // ----- convert stepHits -> MIDI note events (closed hat only, C3) -----
    const int closedHat = 48; // C3
    const int channel = 10;   // GM drums
    // velocity bounds
    int vMin = 70, vMax = 110;
    if (s == "trap" || s == "drill") { vMin = 65; vMax = 105; }

    // helper to add roll using closedHat (preserves your behavior)
    auto addClosedRoll = [&](int t0, int len, bool fast) {
        int sub = fast ? kT32_ : kT16_;
        int t = t0;
        while (t < t0 + len)
        {
            int vel = rng.nextInt({ vMax - vMin + 1 }) + vMin;
            addNote(seq, t, juce::jmax(8, sub / 2), closedHat, vel, channel);
            t += sub;
        }
        };

    for (int step = 0; step < totalSteps; ++step)
    {
        if (stepHits[step] == 0) continue;
        int tick = step * kT16_; // keep same grid units you used (kT16)
        // choose length (dotted/trip handled earlier via majorityTick if needed)
        int len = majorityToTick(majority);
        if (allowDotted && coinInt(12)) len = (int)juce::roundToInt(len * 1.5f);

        int vel = rng.nextInt({ vMax - vMin + 1 }) + vMin;
        // accent rules: accent on bar start sometimes or by random accent
        if ((step % stepsPerBar) == 0 && coinInt((int)juce::roundToInt(100.0f * 0.12f)))
            vel = juce::jmin(127, vel + 18);
        else if (coinInt((int)juce::roundToInt(100.0f * 0.12f)))
            vel = juce::jmin(127, vel + 10);

        // ghost some low-velocity hits
        if (vel < 90 && coinInt((int)juce::roundToInt(100.0f * 0.18f)))
            vel = juce::jmax(24, vel - rng.nextInt({ 34 }));

        // decide whether to roll (style-influenced)
        bool doRoll = false;
        if ((s == "trap" || s == "drill" || s == "r&b") && coinInt((path == 2) ? 50 : 25))
            doRoll = true;

        if (doRoll)
            addClosedRoll(tick, len, coinInt(50));
        else
            addNote(seq, tick, juce::jmax(10, len / 2), closedHat, vel, channel);

        // optional micro-ghost after note
        if (rng.nextInt({ 100 }) < 8)
        {
            int nextTick = juce::jmin(totalSteps * kT16_ - 1, tick + kT16_ / 2);
            addNote(seq, nextTick, juce::jmax(1, kT16_ / 4), closedHat, juce::jmax(8, vel - 22), channel);
        }
    }

    // fallback: if no notes got added, supply a simple steady pulse (16th)
    if (seq.getNumEvents() == 0)
    {
        for (int sidx = 0; sidx < stepsPerBar; ++sidx)
        {
            int tick = sidx * kT16_;
            addNote(seq, tick, juce::jmax(10, kT16_ / 2), closedHat, 80, channel);
        }
    }

    // tempo meta for exported standalone contexts (non-invasive)
    seq.addEvent(juce::MidiMessage::tempoMetaEvent(0.5), 0); // 120bpm default

    seq.updateMatchedPairs();
    return seq;
}


// ===== generateHiHatBatch =====
void BoomAudioProcessor::generateHiHatBatch(const juce::String& style, int tsNum, int tsDen,
    int bars, int howMany, const juce::File& folder,
    bool allowTriplets, bool allowDotted)
{
    howMany = juce::jlimit(1, 100, howMany);
    bars = juce::jlimit(1, 8, bars);

    for (int i = 0; i < howMany; ++i)
    {
        const int seed = (int)((juce::Time::getMillisecondCounter() ^ (i * 2654435761u)) & 0x7fffffff);
        auto seq = makeHiHatPattern(style, tsNum, tsDen, bars, allowTriplets, allowDotted, seed);

        juce::MidiFile mf;
        mf.setTicksPerQuarterNote(kPPQ);
        mf.addTrack(seq);

        juce::File out = folder.getChildFile(
            juce::String("hihat_") + style.replaceCharacter(' ', '_')
            + "_" + juce::String(tsNum) + "of" + juce::String(tsDen)
            + "_" + juce::String(bars) + "bars_"
            + juce::String(i + 1).paddedLeft('0', 2) + ".mid");

        juce::FileOutputStream fos(out);
        if (fos.openedOk())
            mf.writeTo(fos);
    }
}

// --- helpers used below (you already have similar in Rolls) ---
// steps per bar from "N/D" (used for simple gate; all export stays 96 PPQ)
static inline int stepsPerBarFrom(const juce::String& ts)
{
    auto parts = juce::StringArray::fromTokens(ts, "/", "");
    const int n = parts.size() > 0 ? parts[0].getIntValue() : 4;
    const int d = parts.size() > 1 ? parts[1].getIntValue() : 4;
    if (d == 4) return n * 4; // 16th-grid feel
    if (d == 8) return n * 2; // treat 8th as base
    if (d == 16) return n; // super fine
    return n * 4;
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
// aiStartCapture(...)
// Mark as capturing (keep both flag families in sync)
    if (src == CaptureSource::Loopback) {
        ai_rh_rec_.store(true);
        recRh_.store(true);
    }
    else {
        ai_bx_rec_.store(true);
        recBx_.store(true);
    }


    if (auto* ed = getActiveEditor()) ed->repaint();
}

void BoomAudioProcessor::aiStopCapture(CaptureSource src)
{
    // aiStopCapture(...)
    if (src == CaptureSource::Loopback) {
        ai_rh_rec_.store(false);
        recRh_.store(false);
    }
    else {
        ai_bx_rec_.store(false);
        recBx_.store(false);
    }

}

const juce::AudioBuffer<float>& BoomAudioProcessor::getAiBuffer(CaptureSource src) const noexcept
{
    return (src == CaptureSource::Loopback) ? ai_rh_buf_ : ai_bx_buf_;
}

int BoomAudioProcessor::getAiWriteIndex(CaptureSource src) const noexcept
{
    return (src == CaptureSource::Loopback) ? ai_rh_write_ : ai_bx_write_;
}

int BoomAudioProcessor::getAiBufferNumSamples(CaptureSource src) const noexcept
{
    return (src == CaptureSource::Loopback) ? ai_rh_buf_.getNumSamples() : ai_bx_buf_.getNumSamples();
}

// === Simple façade methods used by AIToolsWindow ===
void BoomAudioProcessor::ai_beginRhRecord()
{
    aiStartCapture(CaptureSource::Loopback);
}
void BoomAudioProcessor::ai_endRhRecord()
{
    aiStopCapture(CaptureSource::Loopback);
}
bool BoomAudioProcessor::ai_isRhRecording() const noexcept
{
    return ai_rh_rec_.load();
}

void BoomAudioProcessor::ai_beginBxRecord()
{
    aiStartCapture(CaptureSource::Microphone);
}
void BoomAudioProcessor::ai_endBxRecord()
{
    aiStopCapture(CaptureSource::Microphone);
}
bool BoomAudioProcessor::ai_isBxRecording() const noexcept
{
    return ai_bx_rec_.load();
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

