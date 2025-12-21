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
#include "DrumGenerator.h"
#include "GridUtils.h"
#include "DrumStyleProfileResolver.h"
#include "RollStyleProfiles.h"
#include "RollStyleProfileResolver.h"
#include "HatStyleProfileResolver.h"


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
        true              // default: locked
    ));

    // ADDED: UI BPM parameter (editor attaches a slider to "bpm")
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        "bpm",            // ID - must match editor attachment
        "BPM",            // Name
        juce::NormalisableRange<float>(40.0f, 240.0f, 1.0f),
        120.0f            // default
    ));

    p.push_back(std::make_unique<juce::AudioParameterChoice>("engine", "Engine", boom::engineChoices(), (int)boom::Engine::Drums));
    p.push_back(std::make_unique<juce::AudioParameterChoice>("timeSig", "Time Signature", boom::timeSigChoices(), 0));
    p.push_back(std::make_unique<juce::AudioParameterChoice>("bars", "Bars", boom::barsChoices(), 0));

    p.push_back(std::make_unique<juce::AudioParameterFloat>("humanizeTiming", "Humanize Timing", juce::NormalisableRange<float>(0.f, 100.f), 0.f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("humanizeVelocity", "Humanize Velocity", juce::NormalisableRange<float>(0.f, 100.f), 0.f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("swing", "Swing", juce::NormalisableRange<float>(0.f, 100.f), 0.f));

    p.push_back(std::make_unique<juce::AudioParameterBool>("useTriplets", "Use Triplets", false));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("tripletDensity", "Triplet Density",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterBool>("useDotted", "Use Dotted", false));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("dottedDensity", "Dotted Density",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterBool>("mode_GHXSTGRID", "GHXSTGRID", false));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        "ghxst_intensity",
        "GHXST Intensity",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f),
        35.0f)); // default: subtle

    p.push_back(std::make_unique<juce::AudioParameterBool>("mode_BounceSync", "BounceSync", false));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        "bouncesync_strength",
        "BounceSync Strength",
        juce::StringArray{ "Light", "Medium", "Hard" },
        1)); // default: Medium

    // ------------------- NEW RHYTHM MODES -------------------
// NegSpace: intentionally remove expected hits (gap %) to create space
    p.push_back(std::make_unique<juce::AudioParameterBool>("mode_NegSpace", "NegSpace", false));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        "negspace_gapPct", "NegSpace Gap %", juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 25.0f));

    // TripFlip: triplet hybrids and variants (Off/Light/Normal/Aggressive)
    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        "mode_TripFlip", "TripFlip Mode", juce::StringArray{ "Off", "Light", "Normal", "Aggressive" }, 2));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        "tripflip_density", "TripFlip Density", juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 50.0f));

    // PolyGod: polyrhythm overlay toggle + ratio choice
    p.push_back(std::make_unique<juce::AudioParameterBool>("mode_PolyGod", "PolyGod", false));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        "polygod_ratio", "PolyGod Ratio", juce::StringArray{ "3:4", "5:4", "7:4", "3:2", "5:3" }, 0));

    // Scatter: percussion scatter / percs-special window toggle + params
    p.push_back(std::make_unique<juce::AudioParameterBool>("mode_Scatter", "Scatter", false));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        "scatter_depth", "Scatter Depth", juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 40.0f));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        "scatter_density", "Scatter Density", juce::StringArray{ "Mild", "Normal", "Spicy" }, 1));


    p.push_back(std::make_unique<juce::AudioParameterChoice>("key", "Key", boom::keyChoices(), 0));
    p.push_back(std::make_unique<juce::AudioParameterChoice>("scale", "Scale", boom::scaleChoices(), 0));
    p.push_back(std::make_unique<juce::AudioParameterChoice>("octave", "Octave", juce::StringArray("-2", "-1", "0", "+1", "+2"), 2));

    // Keep the engine-specific rest params (existing)
    p.push_back(std::make_unique<juce::AudioParameterFloat>("restDensity808", "Rest Density 808", juce::NormalisableRange<float>(0.f, 100.f), 10.f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("restDensityDrums", "Rest Density Drums", juce::NormalisableRange<float>(0.f, 100.f), 5.f));

    // NEW: Generic restDensity param — many call-sites expect "restDensity".
    // Default chosen to 5 (you can change to 10 if you'd rather match 808 default).
    p.push_back(std::make_unique<juce::AudioParameterFloat>("restDensity", "Rest Density (Generic)", juce::NormalisableRange<float>(0.f, 100.f), 5.f));

    p.push_back(std::make_unique<juce::AudioParameterChoice>("bassStyle", "Bass Style", boom::styleChoices(), 0));

    p.push_back(std::make_unique<juce::AudioParameterChoice>("drumStyle", "Drum Style", boom::styleChoices(), 0));
    p.push_back(std::make_unique<juce::AudioParameterBool>("rollsTuned", "Rolls Tuned", false));
    p.push_back(std::make_unique<juce::AudioParameterBool>("hatsTuned", "Hats Tuned", false));

    p.push_back(std::make_unique<juce::AudioParameterInt>("seed", "Seed", 0, 1000000, 0));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        "styleBlend",                    // ID used by the editor (styleBlend)
        "Style Blend",                   // human-readable name
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f),
        50.0f                             // default (50% blend)
    ));

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

static int degreeToPitchGlobal(int degreeIndex, int octave,
    const std::vector<int>& scalePCs,
    int keyIndex)
{
    auto wrap12 = [](int v) { v %= 12; if (v < 0) v += 12; return v; };

    if (scalePCs.empty())
        return juce::jlimit(0, 127, octave * 12 + wrap12(keyIndex));

    const int scaleSize = (int)scalePCs.size();
    int di = degreeIndex % scaleSize;
    if (di < 0) di += scaleSize;
    const int pc = scalePCs[di];
    int pitch = octave * 12 + wrap12(keyIndex + pc);
    return juce::jlimit(0, 127, pitch);
}

// -------------------- generateRollPattern helper --------------------
// Insert into PluginProcessor.cpp near other generator helpers.
// Generates single-voice roll patterns using majority notes. Respects "tuned" snapping.
void BoomAudioProcessor::generateRollPattern(juce::MidiMessageSequence& seq,
    const juce::Array<int>& majorityNotes,
    const juce::String& style,
    int bars,
    int ppq,
    int seed,
    bool tuned,
    int keyRoot,
    const std::vector<int>& scalePCs)
{
    // local RNG (deterministic from seed so batches are repeatable)
    juce::Random rng(seed);

    // timing basics
    const int beatsPerBar = 4;
    const int stepsPerBeat = 8;    // work at 32nd-resolution internally for fine timing (32 steps/bar)
    const int stepsPerBar = beatsPerBar * stepsPerBeat; // 32
    const int totalSteps = bars * stepsPerBar;
    const int stepTicks = ppq / stepsPerBeat; // ppq=96 => 12 ticks (32nd)

    // style tuning (how often change note, how many gaps, roll probability)
    int changeIntervalSteps = stepsPerBar / 2; // default: change every half-bar (16 steps)
    int rollStartPct = 8;     // percent chance to spawn a roll around a change boundary
    int restPctBase = getPct(apvts, "restDensity", 20); // use your APVTS getter if present (fallback handled below)
    int longGapBias = 0;      // bias to create more gaps (hiphop -> higher)

    if (style == "drill") { changeIntervalSteps = stepsPerBar / 4; rollStartPct = 10; longGapBias = 8; }
    else if (style == "trap") { changeIntervalSteps = stepsPerBar / 2; rollStartPct = 18; longGapBias = 4; }
    else if (style == "wxstie") { changeIntervalSteps = stepsPerBar; rollStartPct = 22; longGapBias = -6; } // more sustained notes, melodic
    else if (style == "hip hop" || style == "hiphop") { changeIntervalSteps = stepsPerBar / 2; rollStartPct = 6; longGapBias = 18; }
    else { /* leave defaults for other styles */ }

    // protective fallback for APVTS getter — if getPct(...) not available, just default (above)
    // (If your project uses a different APVTS helper, replace getPct(...) with that function.)

    // Utility lambdas
    auto chance = [&](int pct)->bool { return rng.nextInt(100) < juce::jlimit(0, 100, pct); };
    auto iRand = [&](int a, int b)->int { return rng.nextInt((b - a) + 1) + a; };

    // Helper to snap pitched hat to scale if requested.
    auto snapToScaleIfNeeded = [&](int midiNote)->int
        {
            if (!tuned) return midiNote;
            // If your degreeToPitchLocal expects a degree index and octave, adapt accordingly.
            // Here: we choose nearest degree in scale to the input midiNote pitch class.
            int midiPc = midiNote % 12;
            int bestNote = midiNote;
            int bestDist = 128;
            // look +/- 2 octaves for nearest degree in scale
            for (int octaveOffset = -2; octaveOffset <= 2; ++octaveOffset)
            {
                for (int deg = 0; deg < (int)scalePCs.size(); ++deg)
                {
                    int scalePc = wrap12(scalePCs[deg] + keyRoot);
                    int candidate = (midiNote / 12 + octaveOffset) * 12 + scalePc;
                    int dist = std::abs(candidate - midiNote);
                    if (dist < bestDist) { bestDist = dist; bestNote = candidate; }
                }
            }
            return bestNote;
        };

    // Choose initial pivot pitch (use majority notes — convert degrees if tuned)
    auto pickMajorityPitch = [&]() -> int
        {
            if (majorityNotes.isEmpty())
                return 60; // fallback
            int choice = majorityNotes[rng.nextInt(majorityNotes.size())];
            // If your majorityNotes are stored as scale degrees (0..n-1) and tuned==true, you might want:
            //    if (tuned) return degreeToPitchLocal(choice, 4); // example.
            // Here we assume majorityNotes are raw midi pitches or degree indexes depending on your project.
            if (tuned)
            {
                // If majorityNotes contain degree indexes (0..n-1), convert using degreeToPitchGlobal
                if (choice >= 0 && choice <= 11)
                    return degreeToPitchGlobal(choice, 4, scalePCs, keyRoot);
            }
            return choice;
        };

    // single-voice generator loop: at quarter/bar boundaries we may change the main pivot pitch
    int currentPitch = pickMajorityPitch();
    currentPitch = snapToScaleIfNeeded(currentPitch);

    for (int step = 0; step < totalSteps; ++step)
    {
        // Decide to skip some steps based on global rest density and style bias
        int localRestPct = juce::jlimit(0, 100, restPctBase + longGapBias);
        if (chance(localRestPct))
        {
            // a gap; but occasionally we still insert a short ornament
            if (chance(8))
            {
                // micro-ornament (very short)
                int ot = step * stepTicks;
                int vel = iRand(60, 100);
                addNote(seq, ot, stepTicks / 2, currentPitch, vel, 0); // channel 0 for melodic roll (or your melodic channel)
            }
            continue;
        }

        // At change boundaries (every changeIntervalSteps) choose whether to move pivot note
        if ((step % changeIntervalSteps) == 0 && chance(70)) // 70% chance to change at the boundary
        {
            // either pick a different majority pitch or adjust by +/- interval
            if (chance(55))
            {
                currentPitch = pickMajorityPitch();
            }
            else
            {
                // step-wise change (up/down a scale step or a small interval)
                int dir = chance(50) ? 1 : -1;
                currentPitch = currentPitch + dir * iRand(2, 5); // move 2..5 semitones
            }
            currentPitch = snapToScaleIfNeeded(currentPitch);
        }
        RollStyleRuleSet rules = resolveRollRules(style);

        // Every X steps there is a chance to launch a slower, spread-out roll (descending or ascending)
        if (chance(rollStartPct))
        {
            // roll length: 4..12 steps (32nd steps) => spans ~1/8 to 3/8 bar typically
            int rollSteps = 4 + rng.nextInt(9); // 4..12

            int dirRoll = rng.nextInt({ 100 });
            int direction = 0;

            if (dirRoll < rules.ascendPct)
                direction = 1;
            else if (dirRoll < rules.ascendPct + rules.descendPct)
                direction = -1;
            else
                direction = 0; // stationary

            // pitch movement size (no movement if stationary)
            int pitchStep = (direction == 0)
                ? 0
                : (chance(60) ? 1 : 2); // semitone or whole tone

            int baseTick = step * stepTicks;

            // produce single-voice roll (no simultaneous notes)
            for (int r = 0; r < rollSteps; ++r)
            {
                int t = baseTick + r * stepTicks; // 32nd spaced
                int p = currentPitch + direction * r * pitchStep;
                p = snapToScaleIfNeeded(p);

                int vel = iRand(74, 110) - (r * 3); // slight decay
                addNote(seq, t, stepTicks, p, juce::jlimit(1, 127, vel), 0);
            }

            // advance the step index so we don't double-fill the roll area
            step += rollSteps - 1;
            continue;
        }

        // Otherwise write a single long-ish note that sustains until next change boundary
        int tick = step * stepTicks;
        // target length: until next change boundary or a quarter-bar by default
        int stepsToNextChange = changeIntervalSteps - (step % changeIntervalSteps);
        int noteLenSteps = juce::jmax(stepsToNextChange, stepsPerBeat); // at least a beat
        int noteLenTicks = noteLenSteps * stepTicks;

        int velocity = iRand(64, 106);
        addNote(seq, tick, noteLenTicks, currentPitch, velocity, 0);
    } // end step loop
}


void BoomAudioProcessor::buildPatternForPreview()
{
    // Convert the stored `currentPattern` into the processor's public `drumPattern`
    // so the editor and any preview UI can display the generated pattern.
    //
    // setDrumPattern(...) will call notifyPatternChanged() and invoke any
    // drumPatternChangedCallback that the UI has registered.
    setDrumPattern(currentPattern);
}

void BoomAudioProcessor::setGeneratedPattern(const DrumPattern& p)
{
    currentPattern = p;
    buildPatternForPreview(); // converts pattern to preview grid data
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

    // time signature -> steps per bar (keeps your previous logic)
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
    const int ppq = PPQ; // keep your project PPQ constant
    // choose cells-per-beat depending on triplet usage (3 -> triplets, 4 -> 16ths)
    const int cellsPerBeat = allowTriplets ? 3 : 4;

    // ticks-per-step derived from PPQ and chosen cells-per-beat
    const int tps = boom::grid::ticksPerStepFromPpq(ppq, cellsPerBeat);

    const int totalSteps = stepsPerBar * bars;

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
            if (scaleSize == 0) return midiPitch;
            const int rootPC = wrap12(keyIndex + scalePCs[0]);
            const int pc = wrap12(midiPitch % 12);
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

    // TickGuard: keep the same idea but use a bucket size relative to tps to avoid micro-clumping
    TickGuard guard;
    guard.bucketSize = juce::jmax(1, ppq / 8); // prevents micro-clumping; tweak if needed



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
    const int rootDegree = 0;
    const int fifthDegree = juce::jmin(4, scaleSize - 1);

    for (int step = 0; step < totalSteps; ++step)
    {
        if (stepHits[step] == 0) continue;

        // use helper to map step index to PPQ ticks (keeps grid/export consistent)
        int startTick = boom::grid::stepIndexToPpqTicks(step, ppq, cellsPerBeat);

        // choose velocity
        int vel = 80 + (int)rng.nextInt({ 24 }); // 80..103

        // pick pitch choice by weights
        int pitchMidi = degreeToPitchLocal(rootDegree, baseOct);
        const int r = rng.nextInt({ 100 });
        if (r < 70)
        {
            pitchMidi = degreeToPitchLocal(rootDegree, baseOct);
        }
        else if (r < 85)
        {
            pitchMidi = degreeToPitchLocal(fifthDegree, baseOct);
        }
        else if (r < 95)
        {
            const int dir = rng.nextBool() ? 1 : -1;
            pitchMidi = juce::jlimit(0, 127, degreeToPitchLocal(rootDegree, baseOct + dir));
        }
        else
        {
            int approx = degreeToPitchLocal(rootDegree, baseOct);
            int plusminus = rng.nextBool() ? 1 : -1;
            pitchMidi = snapToScaleClosest(approx + plusminus);
        }

        // length in ticks (respect triplet/dotted options) - base is 1 grid step
        int lenTicks = boom::grid::stepsToPpqTicksLen(1, ppq, cellsPerBeat); // 1 step -> ticks

        // when allowing triplets we still used cellsPerBeat==3 above so step size already reflects triplet subdivision.
        // But if you want an alternate behavior (e.g. keep cellsPerBeat==4 and shorten lenTicks for triplet ornament),
        // change the logic here accordingly.
        const bool allowTriplets =
            apvts.getRawParameterValue("allowTriplets")->load() > 0.5f;
        const bool allowDotted =
            apvts.getRawParameterValue("allowDotted")->load() > 0.5f;

        if (allowTriplets && pct(25))
            lenTicks = juce::jmax(1, (int)juce::roundToInt((double)lenTicks * (2.0 / 3.0))); // shorten ~2/3
        if (allowDotted && pct(10))
            lenTicks = juce::jmax(1, (int)juce::roundToInt((double)lenTicks * 1.5));

        // ensure not absurdly tiny: use grid helper instead of hard-coded divisor  
        lenTicks = juce::jmax(lenTicks, boom::grid::ticksPerStepFromPpq(ppq, 24));  

        // add main note
        mp.add({ pitchMidi, startTick, lenTicks, vel });

        // occasional 1/32 split/roll (short tap after main note)
        const float splitProb = 0.16f; // modest rate
        if (rng.nextFloat() < splitProb)
        {
            const int splitTick = juce::jmin(totalSteps * tps - 1, startTick + (tps / 2));
            const int vel2 = juce::jlimit(10, 127, vel - 18);
            int pitch2 = pitchMidi;
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
    // inside BoomAudioProcessor::generate808(...)
    const int stepsPerBar = stepsPerBarFor(tsNum, tsDen);
    // const int tps = 24;                         // old literal
    const int tps = boom::grid::ticksPerStepFromPpq(PPQ, 4); // ticks per 16th derived from PPQ
    const int ppq = PPQ;                         // for export (not used here)
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
    // Quick runtime instrumentation to confirm the plugin is being instantiated
    DBG("BoomAudioProcessor: constructor called");
    juce::Logger::writeToLog("BoomAudioProcessor constructed");
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
    auto addDrum = [&](int row, int step16, int len16, int vel)
        {
            pat.add({ 0, row, toTick16(step16), toTick16(len16), vel });
        };

    // Rows used by existing code
    int rollRow = 1, hatRow = 2;

    // tuned flag read (new param)
    const bool rollsTuned = readParam(apvts, "rollsTuned", 0.0f) > 0.5f;

    // read key/scale for melodic mapping if needed
    juce::String keyName = "C";
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("key")))
        keyName = p->getCurrentChoiceName();

    juce::String scaleName = "Chromatic";
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("scale")))
        scaleName = p->getCurrentChoiceName();

    // find scale pitch classes
    auto itScale = kScales.find(scaleName.trim());
    const auto& scalePCs = (itScale != kScales.end()) ? itScale->second : kScales.at("Chromatic");
    const int scaleSize = (int)scalePCs.size();
    const int keyIndex = juce::jmax(0, kKeys.indexOf(keyName.trim().toUpperCase()));

    auto wrap12 = [](int v) { v %= 12; if (v < 0) v += 12; return v; };
    auto degreeToPitchLocal = [&](int degreeIndex, int octave) -> int
        {
            if (scaleSize == 0) return juce::jlimit(0, 127, octave * 12 + wrap12(keyIndex));
            int di = degreeIndex % scaleSize; if (di < 0) di += scaleSize;
            const int pc = scalePCs[di];
            return juce::jlimit(0, 127, octave * 12 + wrap12(keyIndex + pc));
        };

    // Reserve a local small melodic buffer for tuned roll notes (so we can hand off or export)
    juce::Array<MelNote> tunedNotes; tunedNotes.clear();

    // core style branches (preserve your original ideas but inject tuned behaviour)
    if (style.compareIgnoreCase("trap") == 0)
    {
        for (int b = 0; b < bars; ++b)
        {
            const int start = b * 16;
            for (int i = 0; i < 16; ++i)
            {
                // Hats support for groove
                const bool hatsTuned = readParam(apvts, "hatsTuned", 0.0f) > 0.5f;

                // assume total16 already defined (bars * 16)
                auto pat = getDrumPattern();
                pat.clear();
                const int total16 = q16(bars);

                // add() helper lambda — insert this if you do not already have an `add` lambda in this function
                auto add = [&](int row, int step16, int len16, int vel)
                    {
                        pat.add({ 0 /*chan*/, row, toTick16(step16), toTick16(len16), vel });
                    };
                for (int i = 0; i < total16; ++i)
                {
                    if (hatsTuned && chance(35))
                    {
                        // Multi-timbre layer (safe default)
                        // primary closed hat (row 2)
                        add(2, i, 1, irand(70, 95));

                        // layer: additional metallic/pitched hat on alternate row (row 3)
                        // If you do not have an alternate row, change 3 -> correct row index or remove this line
                        add(3, i, 1, irand(50, 85));
                    }
                    else
                    {
                        // Default behavior — note the logical OR (||) and parentheses around the modulus check
                        if (((i % 2) == 0) || chance(20))
                            add(2, i, 1, irand(70, 95));

                        if (chance(12) && (i + 1 < total16))
                            add(2, i + 1, 1, irand(60, 85));
                    }
                }

                // Decide whether this should be a tuned melodic roll instead of a straight snare hit
                if (rollsTuned && chance(35))
                {
                    // choose ascending or descending
                    const bool asc = chance(50);
                    // choose base degree & octave (keeps things mid-range)
                    int baseDegree = 0; // root by default
                    // choose a base degree near root or third for musical results
                    if (scaleSize >= 3) baseDegree = (scaleSize >= 5) ? 2 : 1;
                    int baseOct = 4; // middle octave for tonal rolls (tweakable)
                    // density: we can step every 1/16 or occasionally step by 1/32 for trills
                    for (int r = 0; r < 4; ++r) // 4-step short melodic roll (can be 2..8)
                    {
                        int stepOffset = i + r; // each subsequent step advances by a 16th (simple)
                        if (stepOffset >= 16) break;
                        int degreeDelta = asc ? r : -r;
                        int pitch = degreeToPitchLocal(baseDegree + degreeDelta, baseOct);
                        // push to tuned notes (MelNote uses pitch, startTick)
                        tunedNotes.add({ pitch, toTick16(start + stepOffset), toTick16(1), irand(70, 110), 1 });
                        // Also add a soft snare/his row for body if you like:
                        if (r == 0) addDrum(rollRow, start + i, 1, irand(80, 110));
                    }
                }
                else
                {
                    // normal non-tuned behavior
                    if (chance(25)) addDrum(rollRow, start + i, 1, irand(80, 110));
                    if (chance(10) && i < 15) addDrum(rollRow, start + i + 1, 1, irand(70, 95)); // small 32nd echo
                }
            }
        }
    }
    else if (style.compareIgnoreCase("drill") == 0)
    {
        for (int b = 0; b < bars; ++b)
        {
            const int start = b * 16;
            // accent near beat end
            addDrum(rollRow, start + 12, 1, irand(100, 120));

            for (int t = 0; t < 6; ++t)
            {
                int p = start + (t * 2);
                if (p < start + 16)
                {
                    if (rollsTuned && chance(35))
                    {
                        // shorter melodic ornaments that sit on those p positions
                        const bool asc = chance(50);
                        int baseDeg = 2, baseOct = 4;
                        for (int r = 0; r < 3; ++r)
                        {
                            int step = p + r;
                            if (step >= start + 16) break;
                            int degOff = asc ? r : -r;
                            tunedNotes.add({ degreeToPitchLocal(baseDeg + degOff, baseOct),
                                             toTick16(step), toTick16(1), irand(75, 105), 1 });
                        }
                        addDrum(rollRow, p, 1, irand(70, 100));
                    }
                    else if (chance(60))
                    {
                        addDrum(rollRow, p, 1, irand(75, 100));
                    }
                }
            }

            for (int i = 0; i < 16; ++i)
                if (i % 2 == 0 || chance(20)) addDrum(hatRow, start + i, 1, irand(60, 85));
        }
    }
    else if (style.compareIgnoreCase("edm") == 0)
    {
        // (keep your existing EDM logic; we can overlay tuned if desired)
        int seg = bars * 16;
        for (int i = 0; i < seg; ++i)
        {
            if ((i % 4) == 0) addDrum(rollRow, i, 1, irand(90, 120));
            if (chance(10)) addDrum(hatRow, i, 1, irand(70, 95));
        }
    }
    else
    {
        // fallback: steady hats and occasional snares -- preserve original behavior
        for (int i = 0; i < total16; ++i)
        {
            if (i % 2 == 0 || chance(20)) addDrum(2, i, 1, irand(70, 95));
            if (chance(12) && i + 1 < total16) addDrum(2, i + 1, 1, irand(60, 85));
        }
    }

    // if we created tuned melodic notes, merge them into melodicPattern so piano-roll/editor can show them
    if (tunedNotes.size() > 0)
    {
        auto mp = getMelodicPattern();
        // append tuned ornaments (do not clear existing melodic pattern)
        for (auto& mn : tunedNotes)
            mp.add(mn);
        setMelodicPattern(mp);
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
    RollStyleRuleSet rules = resolveRollRules(style);
    int sanity = rules.rollStartPct;

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
    if ((!allowTriplets || !rules.favorTriplets)  &&
        (majority == 3 || majority == 4))
    {
        majority = 1;
    }
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
    int r = rng.nextInt({ 100 });

    if (r < rules.steadyPulsePct)
        path = 0; // steady
    else if (r < rules.steadyPulsePct + rules.gapPct)
        path = 1; // gappy
    else
        path = 2; // risky


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
        if (wild || coinInt(rules.endOfBarFillPct))
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
        const bool allowTriplets =
            apvts.getRawParameterValue("allowTriplets")->load() > 0.5f;
        const bool allowDotted =
            apvts.getRawParameterValue("allowDotted")->load() > 0.5f;
        if (allowTriplets && coinInt(25)) len
            = juce::jmax(1, (int)juce::roundToInt((double)len * (2.0 / 3.0))); // shorten ~2/3
        if (allowDotted && coinInt(12)) len = (int)juce::roundToInt(len * 1.5f);

        int baseVel = rng.nextInt({ vMax - vMin + 1 }) + vMin;

        // accent rule: stronger on bar boundaries or first events of clusters
        if ((step % stepsPerBar) == 0 && coinInt(60))
            baseVel = juce::jmin(127, baseVel + 18);
        else if (coinInt(35))
            baseVel = juce::jmin(127, baseVel + 10);
        bool wild = coinInt(rules.rollWildPct);
        // decide roll vs single (style influenced and wild path)
        int rollChance =
            (path == 2)
            ? rules.rollChancePct + 20
            : rules.rollChancePct;

        bool doRoll = coinInt(rollChance);

        // slightly higher chance to flam on seed-sourced patterns (gives human feel)
        bool doFlam = (!useSeeds && coinInt(8)) || (useSeeds && coinInt(20));

        if (doRoll)
        {
            // roll length bias: short (tight) for trap/drill, longer for r&b/pop
            bool fast = coinInt(rules.fastRollPct);
            addSnareRoll(tick, len, fast);
        }
        else if (doFlam && coinInt(rules.flamPct))
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

// ---------- REPLACE entire BoomAudioProcessor::generateDrums(int bars) with this ----------
void BoomAudioProcessor::generateDrums(int bars)
{

const int tripletPct = (int) apvts.getRawParameterValue("tripletDensity")->load();
const int dottedPct  = (int) apvts.getRawParameterValue("dottedDensity")->load();

const bool allowTriplets = apvts.getRawParameterValue("allowTriplets")->load() > 0.5f;
const bool allowDotted   = apvts.getRawParameterValue("allowDotted")->load() > 0.5f;

juce::Random rng((int) genNonce_.fetch_add(1));
    // clamp bars to a safe range
    bars = juce::jlimit(1, 8, bars);

    // read chosen drum style from APVTS (safe fallback to "trap")
    juce::String style = "trap";
    if (auto* prm = apvts.getParameter("drumStyle"))
        if (auto* ch = dynamic_cast<juce::AudioParameterChoice*>(prm))
            style = ch->getCurrentChoiceName();

    // ensure style exists in DB (fallback to first style if necessary)
    auto styles = boom::drums::styleNames();
    if (!styles.contains(style) && styles.size() > 0)
        style = styles[0];

    // Read percent sliders from APVTS using existing helper getPct(apvts, key, default)
    const int restPct = getPct(apvts, "restDensityDrums", 0);
    const int swingPct = getPct(apvts, "swing", 0);

    DBG("generateDrums: style=" << style
        << " bars=" << bars
        << " restPct=" << restPct
        << " dotted=" << dottedPct
        << " triplet=" << tripletPct
        << " swing=" << swingPct);

    // near the top of generateDrums(...), after reading tripletPct/dottedPct:
    const bool useTriplets = (apvts.getRawParameterValue("useTriplets") ? apvts.getRawParameterValue("useTriplets")->load() > 0.5f : false);
    const bool useDotted = (apvts.getRawParameterValue("useDotted") ? apvts.getRawParameterValue("useDotted")->load() > 0.5f : false);

    // Debug output to confirm we're reading UI state
    DBG("generateDrums: useTriplets=" << (useTriplets ? 1 : 0) << " tripletPct=" << tripletPct
        << " useDotted=" << (useDotted ? 1 : 0) << " dottedPct=" << dottedPct);


    // Use the canonical generator (preserves sub-16th) to produce a starting pattern
    boom::drums::DrumStyleSpec spec = boom::drums::getSpec(style);
    boom::drums::DrumPattern pat;
    boom::drums::generate(spec, bars, restPct, dottedPct, tripletPct, swingPct, /*seed*/ -1, pat);

    // Convert generator DrumPattern -> processor internal Pattern (keep exact ticks)
    BoomAudioProcessor::Pattern out;
    out.ensureStorageAllocated((int)pat.size());
    for (const auto& e : pat)
    {
        BoomAudioProcessor::Note n;
        n.row = e.row;
        n.startTick = e.startTick;
        n.lengthTicks = e.lenTicks;
        n.velocity = juce::jlimit<int>(1, 127, (int)e.vel);
        out.add(n);
    }

    // ---------- 1) Remove spurious "super small" ghost notes ----------
    // Some generator paths may leave extremely short notes that look like garbage.
    // Remove notes with lengthTicks < minLenTicks OR velocity <= 1.
    const int ppq_ = 96;
    const int minLenTicks = boom::grid::ticksPerStepFromPpq(ppq_, 4);

    for (int i = out.size() - 1; i >= 0; --i)
    {
        const auto& n = out.getReference(i);
        if (n.lengthTicks < minLenTicks || n.velocity <= 1)
            out.remove(i);
    }

    // ---------- helper: find row index by name (case-insensitive substring) ----------
    auto findRowByNameContains = [this](const juce::String& needle) -> int
        {
            const auto& rows = getDrumRows(); // returns juce::StringArray in this codebase
            const juce::String lowerNeedle = needle.toLowerCase();
            for (int i = 0; i < rows.size(); ++i)
            {
                const juce::String rn = rows[i].toLowerCase();
                if (rn.contains(lowerNeedle))
                    return i;
            }
            return -1;
        };

    // ---------- 2) Ensure Kick always starts at bar 1 beat 1 (tick 0) ----------
    int kickRow = findRowByNameContains("kick");
    if (kickRow < 0) kickRow = 0; // fallback to row 0 if none found

    // check for existing kick at tick 0; if none, insert one
    bool kickAtZero = false;
    for (int i = 0; i < out.size(); ++i)
    {
        const auto& n = out.getReference(i);
        if (n.row == kickRow && n.startTick == 0)
        {
            kickAtZero = true;
            break;
        }
    }
    if (!kickAtZero)
    {
        BoomAudioProcessor::Note k;
        k.row = kickRow;
        k.startTick = 0;
        k.lengthTicks = ppq_ / 4; // quarter-note-ish length by default (safe)
        k.velocity = 110;
        out.insert(0, k); // put it near the front
    }

    // ---------- 3) For TRAP and DRILL: move snares to beat 3 of each bar ----------
    if (style.compareIgnoreCase("trap") == 0 || style.compareIgnoreCase("drill") == 0)
    {
        int snareRow = findRowByNameContains("snare");
        if (snareRow < 0) snareRow = 1; // fallback (kick=0, snare=1 common)

        // time signature helpers
        const int beatsPerBar = getTimeSigNumerator();
        const int ticksPerBeat = ppq_ * 4 / getTimeSigDenominator(); // if you have this helper available
        const int tinyOrnamentLen = juce::jmax(minLenTicks, ticksPerBeat / 6); // about 1/6 of a beat for ornaments
        const int ticksPerBar = ticksPerBeat * beatsPerBar;
        const int beat3Offset = ticksPerBeat * 2; // beat 3 start (0-based)

        for (int i = 0; i < out.size(); ++i)
        {
            auto& n = out.getReference(i);
            if (n.row != snareRow) continue;

            const int barIndex = n.startTick / ticksPerBar;
            const int newStart = barIndex * ticksPerBar + beat3Offset;
            n.startTick = juce::jmax(0, newStart);
        }
    }

    // ---------- 4) Triplet insertion post-process (force triplets to exist when user asked for them) ----------
    // Strategy: for rows that are logical candidates (hats / perc / toms / ride), for each beat
    // we *may* insert two triplet subdivision notes (1/3 and 2/3 of beat) with probability = tripletPct.
    // This is additive (won't remove existing notes), but we avoid duplicates by checking proximity.
    if (tripletPct > 0)
    {
        // find candidate rows by name
        juce::Array<int> candidateRows;
        {
            const auto& rows = getDrumRows();
            for (int i = 0; i < rows.size(); ++i)
            {
                const juce::String rn = rows[i].toLowerCase();
                if (rn.contains("hat") || rn.contains("hi") || rn.contains("perc") || rn.contains("tom") || rn.contains("ride"))
                    candidateRows.add(i);
            }
            // if no candidates found, as a fallback include row 2..4 range (common hat/perc rows)
            if (candidateRows.size() == 0)
            {
                for (int r = 0; r < juce::jmin(6, getDrumRows().size()); ++r)
                    candidateRows.add(r);
            }
        }

        // time signature helpers
        const int beatsPerBar = getTimeSigNumerator();
        const int denom = getTimeSigDenominator();
        const int ticksPerBeat = ppq_ * 4 / denom; // reasonable universal formula
        const int ticksPerBar = ticksPerBeat * beatsPerBar;
        const int tThird = ticksPerBeat / 3; // one third of a beat (~32 ticks at ppq=96)

        // For each bar and beat, consider adding triplet subdivisions on candidate rows
        const int barsToProcess = bars;
        for (int bar = 0; bar < barsToProcess; ++bar)
        {
            for (int beat = 0; beat < beatsPerBar; ++beat)
            {
                const int beatStart = bar * ticksPerBar + beat * ticksPerBeat;
                // decide if this beat should have triplets based on tripletPct
                if (!chance(tripletPct)) continue;

                // We'll add triplet subdivisions probabilistically per candidate row but
                // respect existing generator decisions. For each candidate row, consult the
                // generator-derived pattern density early: if the row already has many notes
                // in this beat window, skip adding triplets to avoid chopping everything.
                for (int rIdx = 0; rIdx < candidateRows.size(); ++rIdx)
                {
                    const int row = candidateRows[rIdx];

                    // Count existing notes in this beat window for this row
                    auto countExistingInWindow = [&](int windowStart, int windowEnd) -> int
                    {
                        int cnt = 0;
                        for (int i = 0; i < out.size(); ++i)
                        {
                            const auto& n = out.getReference(i);
                            if (n.row != row) continue;
                            if (n.startTick >= windowStart && n.startTick < windowEnd) ++cnt;
                        }
                        return cnt;
                    };

                    const int windowStart = beatStart;
                    const int windowEnd = beatStart + ticksPerBeat;
                    const int existing = countExistingInWindow(windowStart, windowEnd);

                    // If there are already >=2 notes in this beat for this row, skip to avoid chopping
                    if (existing >= 2) continue;

                    // avoid overcrowding: check if a note already exists near the triplet positions
                    auto existsNear = [&](int checkTick) -> bool
                    {
                        const int fuzz = juce::jmax(1, ticksPerBeat / 8); // tolerance
                        for (int i = 0; i < out.size(); ++i)
                        {
                            const auto& n = out.getReference(i);
                            if (n.row != row) continue;
                            if (std::abs(n.startTick - checkTick) <= fuzz)
                                return true;
                        }
                        return false;
                    };

                    const int t1 = beatStart + tThird;
                    const int t2 = beatStart + 2 * tThird;

                    const int insertedLen = juce::jmax(minLenTicks, juce::jmax(1, ticksPerBeat / 6));

                    // Add first triplet subdivision (t1) if no nearby note exists
                    if (!existsNear(t1) && chance(juce::jmin(100, tripletPct)))
                    {
                        BoomAudioProcessor::Note tn;
                        tn.row = row;
                        tn.startTick = t1;
                        tn.lengthTicks = insertedLen;
                        tn.velocity = juce::jlimit<int>(40, 127, 80);
                        out.add(tn);
                    }
                    if (!existsNear(t2) && chance(juce::jmin(100, tripletPct)))
                    {
                        BoomAudioProcessor::Note tn2;
                        tn2.row = row;
                        tn2.startTick = t2;
                        tn2.lengthTicks = insertedLen;
                        tn2.velocity = juce::jlimit<int>(30, 127, 70);
                        out.add(tn2);
                    }
                } // candidate rows
            } // beat
        } // bar
    } // tripletPct > 0

    // ---------- final tidy: sort pattern by startTick (so UI/pattern consumers are happy) ----------
    // Assume Pattern supports a simple sort — otherwise build a temporary vector and re-add in order.
    // We'll perform an insertion-sort-like stable reorder if no direct sort available.
    {
        // If Pattern has sort method, call it; otherwise do manual simple sort by copying to vector
        std::vector<BoomAudioProcessor::Note> tmpVec;
        tmpVec.reserve(out.size());
        for (int i = 0; i < out.size(); ++i) tmpVec.push_back(out.getReference(i));
        std::sort(tmpVec.begin(), tmpVec.end(), [](const BoomAudioProcessor::Note& a, const BoomAudioProcessor::Note& b) {
            if (a.startTick != b.startTick) return a.startTick < b.startTick;
            if (a.row != b.row) return a.row < b.row;
            return a.velocity > b.velocity;
            });
        // rebuild 'out'
        out.clear();
        out.ensureStorageAllocated((int)tmpVec.size());
        for (auto& n : tmpVec) out.add(n);
    }

    // ---- GHXSTGRID (improved) ----
    // Sprinkle subtle ghost micro-hits around hats/kicks.
    // This smarter version:
    //  - finds actual hat/perc row indices from drum row names (no hardcoded indexes)
    //  - uses a seed-based RNG (uses the APVTS "seed" param for reproducible results)
    //  - better collision avoidance via a small-tolerance check
    if (readParam(apvts, "mode_GHXSTGRID", 0.0f) > 0.5f)
    {
        // intensity 0..100 -> chance scale
        const int intensity = juce::jlimit(0, 100, (int)juce::roundToInt(readParam(apvts, "ghxst_intensity", 35.0f)));

        // Prefer deterministic behavior for batch/seeded exports: read your existing "seed" param.
        int seedVal = 0;
        if (auto* seedParam = apvts.getRawParameterValue("seed"))
            seedVal = juce::jmax(0, (int)juce::roundToInt(seedParam->load()));
        juce::Random rng(seedVal ^ (int)genNonce_.load()); // mix with genNonce_ for variety across generations if desired

        // Build a list of candidate rows for ghosts by scanning drum row names.
        // Use getDrumRows() accessor (returns juce::StringArray).
        juce::StringArray names = getDrumRows();
        std::vector<int> hatRowIndices;
        std::vector<int> percRowIndices;

        for (int i = 0; i < names.size(); ++i)
        {
            const juce::String n = names[i].toLowerCase();
            if (n.contains("hat") || n.contains("hihat") || n.contains("hh")) hatRowIndices.push_back(i);
            else if (n.contains("perc") || n.contains("shaker") || n.contains("tamb") || n.contains("ride") || n.contains("crash"))
                percRowIndices.push_back(i);
        }

        // Fallbacks if none were identified
        if (hatRowIndices.empty())
        {
            // try some default names / heuristics
            for (int i = 0; i < names.size(); ++i)
            {
                if (names[i].toLowerCase().contains("hat") || names[i].toLowerCase().contains("h")) { hatRowIndices.push_back(i); break; }
            }
        }
        if (hatRowIndices.empty())
        {
            // final fallback: prefer row 2 if it exists, else row 0
            if (names.size() > 2) hatRowIndices.push_back(2);
            else if (names.size() > 0) hatRowIndices.push_back(0);
        }
        if (percRowIndices.empty())
        {
            // fallback to rows after row 2 (common layout)
            for (int i = 3; i < names.size(); ++i) percRowIndices.push_back(i);
        }

        // Collect existing hat onsets (we'll use these anchors to place ghosts around)
        std::vector<int> hatStarts;
        for (const auto& n : out)
        {
            // use any note whose row is one of the hatRowIndices
            for (int hr : hatRowIndices)
            {
                if (n.row == hr)
                {
                    hatStarts.push_back(n.startTick);
                    break;
                }
            }
        }

        // Helper that checks whether a tick/row is already occupied (small tolerance)
        auto occupiedNear = [&](int row, int tick, int tolTicks = 8) -> bool
            {
                for (const auto& existing : out)
                    if (existing.row == row && std::abs(existing.startTick - tick) < tolTicks)
                        return true;
                return false;
            };

        // constants
        const int kStepTicks = 24; // your 16th tick size
        const int tick32 = kStepTicks / 2; // 12 ticks (32nd)

        // For each hat anchor, probabilistically add ghosts
        for (int anchorTick : hatStarts)
        {
            if (rng.nextInt({ 100 }) >= juce::jlimit(0, 100, intensity)) continue;

            // decide 1 or 2 ghosts, velocity & offset scale with intensity
            const int ghosts = (rng.nextInt({ 100 }) < 65) ? 1 : 2;
            for (int g = 0; g < ghosts; ++g)
            {
                // prefer small offsets: 1/32 or 1/16; randomly early/late
                int offsetTicks = (rng.nextInt({ 100 }) < 60) ? tick32 : kStepTicks;
                if (rng.nextBool()) offsetTicks = -offsetTicks;

                // choose a target row: prefer an actual hat row, but sometimes use a nearby perc row
                int targetRow;
                if (!hatRowIndices.empty() && rng.nextInt({ 100 }) < 85)
                {
                    targetRow = hatRowIndices[rng.nextInt(hatRowIndices.size())];
                }
                else if (!percRowIndices.empty())
                {
                    targetRow = percRowIndices[rng.nextInt(percRowIndices.size())];
                }
                else
                {
                    // final fallback: use first hat row
                    targetRow = hatRowIndices.empty() ? 0 : hatRowIndices.front();
                }

                int newStart = anchorTick + offsetTicks;

                if (newStart < 0) continue;

                // avoid collision on the chosen (row,tick)
                if (occupiedNear(targetRow, newStart, 8))
                {
                    // attempt opposite offset direction once
                    newStart = anchorTick - offsetTicks;
                    if (newStart < 0 || occupiedNear(targetRow, newStart, 8))
                        continue; // give up for this ghost
                }

                // velocity: base soft velocity scaled by intensity (more intensity => a bit louder)
                int baseVel = 36 + rng.nextInt({ 28 }); // 36..63
                int vel = juce::jlimit(1, 127, (int)(baseVel + (intensity * 0.2f)));

                // length short
                int len = juce::jmax(6, rng.nextInt({ 12 }));

                BoomAudioProcessor::Note nn;
                nn.row = targetRow;
                nn.startTick = newStart;
                nn.lengthTicks = len;
                nn.velocity = vel;
                out.add(nn);
            }
        }

        // occasional ghost kick (but route only to rows that look like kick)
        // find candidate kick rows by name
        std::vector<int> kickRows;
        for (int i = 0; i < names.size(); ++i)
        {
            const juce::String n = names[i].toLowerCase();
            if (n.contains("kick") || n.contains("bd") || n.contains("bass")) kickRows.push_back(i);
        }
        if (kickRows.empty())
        {
            if (names.size() > 0) kickRows.push_back(0);
        }

        if (rng.nextInt({ 100 }) < juce::jlimit(0, 100, intensity / 6))
        {
            // pick random 16th step within maxTick range
            int maxTick = 0;
            for (const auto& n : out) if (n.startTick > maxTick) maxTick = n.startTick;
            int totalSteps = juce::jmax(1, (maxTick / kStepTicks) + 1);
            int pickStep = rng.nextInt({ totalSteps });
            int pickTick = pickStep * kStepTicks;

            // avoid existing kick nearby
            bool foundKickNearby = false;
            for (const auto& n : out)
                for (int kr : kickRows)
                    if (n.row == kr && std::abs(n.startTick - pickTick) < kStepTicks) foundKickNearby = true;

            if (!foundKickNearby)
            {
                BoomAudioProcessor::Note kk;
                kk.row = kickRows[rng.nextInt(kickRows.size())];
                kk.startTick = pickTick;
                kk.lengthTicks = kStepTicks;
                kk.velocity = juce::jlimit(1, 127, (int)(20 + intensity / 2));
                out.add(kk);
            }
        }

        // keep deterministic ordering for UI / export consistency
        std::sort(out.begin(), out.end(), [](const BoomAudioProcessor::Note& a, const BoomAudioProcessor::Note& b)
            { return a.startTick < b.startTick; });
    }
    // ---- end improved GHXSTGRID ----
    // ---- BounceSync pass ----
// Apply small per-row timing offsets to create a "bounce" syncopation feel.
// Reads APVTS "mode_BounceSync" and "bouncesync_strength".
    if (readParam(apvts, "mode_BounceSync", 0.0f) > 0.5f)
    {
        juce::Random rng; // use non-deterministic RNG; replace with seed-based RNG if you need repeatability
        const int strengthIdx = (int)juce::jlimit(0, 2, (int)juce::roundToInt(readParam(apvts, "bouncesync_strength", 1.0f)));
        // Map strength index -> {offsetTicks, chancePct, per-row bias}
        // offsetTicks: magnitude of timing shift in ticks (kTicksPerStep == 24 for 1/16)
        // chancePct: how likely to apply offset to an eligible note
        int offsetTicks = 0;
        int chancePct = 0;
        switch (strengthIdx)
        {
        case 0: // Light
            offsetTicks = 6;    // ~1/64-ish subtle nudge
            chancePct = 30;   // 30% chance per eligible note
            break;
        case 1: // Medium
            offsetTicks = 12;   // ~1/32
            chancePct = 50;
            break;
        case 2: // Hard
            offsetTicks = 24;   // ~1/16
            chancePct = 75;
            break;
        default:
            offsetTicks = 12;
            chancePct = 50;
            break;
        }

        // Small guard function: check occupancy within a tolerance to avoid collisions
        auto isOccupiedNear = [&](int tickCheck) -> bool
            {
                for (const auto& n : out)
                    if (std::abs(n.startTick - tickCheck) < 8) // 8 ticks tolerance
                        return true;
                return false;
            };

        // We'll modify `out` in-place; to avoid iterator invalidation create indices list.
        for (int i = 0; i < out.size(); ++i)
        {
            auto& note = out.getReference(i);

            // Eligible rows — tune if your row indices differ. typical mapping:
            // row 0 = kick, row 1 = snare/clap, row 2 = closed hat
            // adjust if your project uses different row numbering.
            const int row = note.row;

            // Apply BounceSync only to certain rows, with row-specific bias:
            bool eligible = false;
            int rowBias = 0; // positive => prefer later; negative => prefer earlier
            if (row == 0) { eligible = true; rowBias = -1; }   // kick slightly "ahead"
            if (row == 1) { eligible = true; rowBias = +1; }   // snare slightly "behind"
            if (row == 2) { eligible = true; rowBias = +1; }   // hats slightly "behind"

            if (!eligible) continue;

            // probabilistic application
            if (rng.nextInt({ 100 }) >= chancePct) continue;

            // pick direction: bias tilts probability of late vs early
            int dirRoll = rng.nextInt({ 100 });
            bool applyLater = (dirRoll < (50 + rowBias * 15)); // rowBias changes tilt

            int proposed = note.startTick + (applyLater ? offsetTicks : -offsetTicks);

            // keep bounds
            if (proposed < 0) proposed = 0;

            // avoid creating severe overlap with near notes
            if (isOccupiedNear(proposed))
            {
                // try the opposite direction once
                proposed = note.startTick + (applyLater ? -offsetTicks : offsetTicks);
                if (proposed < 0 || isOccupiedNear(proposed))
                    continue; // skip applying if both occupied
            }

            // Apply micro-humanization: small jitter +/- 1..3 ticks
            int jitter = rng.nextInt({ 7 }) - 3; // -3..+3
            note.startTick = juce::jmax(0, proposed + jitter);
        }
    }
    // ---- end BounceSync pass ----

    // ------------------- Mode passes (NegSpace / TripFlip / PolyGod / Scatter) -------------------
// These mutate `out` in-place. Insert immediately BEFORE `setDrumPattern(out);` in your drum generator finalization.

//
// 1) NegSpace: remove a percentage of non-downbeat events to create 'negative space' pockets.
//
    if (readParam(apvts, "mode_NegSpace", 0.0f) > 0.5f)
    {
        const int gapPct = juce::jlimit(0, 100, (int)juce::roundToInt(readParam(apvts, "negspace_gapPct", 25.0f)));
        juce::Random rng; // random removal distribution
        // prefer removing non-downbeat / non-strong events: we'll keep events on strong beats (step % 4 == 0)
        for (int i = out.size() - 1; i >= 0; --i)
        {
            const auto& n = out.getReference(i);
            const int step16 = n.startTick / 24; // your grid uses 24 ticks per 16th
            const bool isStrong = (step16 % 4) == 0; // downbeat/quarter
            // never remove strong downbeats
            if (isStrong) continue;
            if (rng.nextInt({ 100 }) < gapPct)
                out.remove(i);
        }
    }

    //
    // 2) TripFlip: modifies triplet/dotted behavior by injecting triplet clusters or shifting placement.
    //    Mode defined by param "mode_TripFlip": 0=Off,1=Light,2=Normal,3=Aggressive
    //
    if (readParam(apvts, "mode_TripFlip", 0.0f) >= 1.0f)
    {
        juce::Random rng;
        const int modeIdx = juce::jlimit(0, 3, (int)juce::roundToInt(readParam(apvts, "mode_TripFlip", 2.0f)));
        const int density = juce::jlimit(0, 100, (int)juce::roundToInt(readParam(apvts, "tripflip_density", 50.0f)));

        // We'll create localized triplet micro-clusters around existing strong events (snares/hats)
        // work at 32nd resolution (12 ticks)
        const int tick32 = 12;
        // collect strong onsets (snares/hats)
        std::vector<int> anchors;
        for (const auto& n : out)
            if (n.row == 1 || n.row == 2) // snare or hat
                anchors.push_back(n.startTick);

        for (int anchor : anchors)
        {
            // chance scaled by density and mode
            int baseChance = density;
            baseChance += (modeIdx - 1) * 15; // Light:-15, Normal:0, Aggressive:+15
            if (rng.nextInt({ 100 }) >= juce::jlimit(0, 100, baseChance)) continue;

            // number of micro-notes (1..3)
            int microCount = 1 + (modeIdx >= 2 ? rng.nextInt({ 2 }) : 0); // Normal+ adds chance for 2
            for (int m = 0; m < microCount; ++m)
            {
                // place triplet pattern around anchor: anchor - tick32, anchor + 0, anchor + tick32
                int offsetIdx = (m == 0) ? -1 : ((m == 1) ? 0 : 1);
                int newStart = anchor + offsetIdx * tick32;
                // avoid negative and out-of-range
                if (newStart < 0) continue;
                bool collision = false;
                for (const auto& n2 : out) if (std::abs(n2.startTick - newStart) < 6) { collision = true; break; }
                if (collision) continue;

                BoomAudioProcessor::Note addN;
                addN.row = 2; // place as hat micro-ornament by default
                addN.startTick = newStart;
                addN.lengthTicks = juce::jmax(6, tick32 / 2);
                addN.velocity = juce::jlimit(1, 127, 50 + rng.nextInt({ 40 }) - (modeIdx * 6));
                out.add(addN);
            }
        }
    }

    //
    // 3) PolyGod: add a second overlay voice running at a different subdivision (polyrhythm).
    //    The overlay will be blended lightly, preserving main pattern integrity.
    //
    if (readParam(apvts, "mode_PolyGod", 0.0f) > 0.5f)
    {
        juce::Random rng;
        const int ratioIdx = juce::jlimit(0, 4, (int)juce::roundToInt(readParam(apvts, "polygod_ratio", 0.0f)));
        // mapping choice -> overlayStepsPerBar (relative to 4/4 16 steps)
        // 3:4 -> overlay = 12 steps (3 per beat), 5:4 -> overlay = 20, 7:4 -> 28, 3:2 -> 24 (3 over 2), 5:3 -> ~?
        int overlaySteps = 12;
        switch (ratioIdx)
        {
        case 0: overlaySteps = 12; break; // 3:4
        case 1: overlaySteps = 20; break; // 5:4
        case 2: overlaySteps = 28; break; // 7:4
        case 3: overlaySteps = 24; break; // 3:2
        case 4: overlaySteps = 20; break; // 5:3 mapped to 20 as practical fallback
        default: overlaySteps = 12; break;
        }

        // Determine bars length (infer from max tick in `out`)
        int maxTick = 0;
        for (const auto& n : out) maxTick = juce::jmax(maxTick, n.startTick);
        const int total16steps = (maxTick / 24) + 1;
        const int totalOverlaySteps = (total16steps * 16 / 16) * (overlaySteps / 16 + 0); // rough estimate

        // simple overlay: place soft percussive clicks across overlaySteps grid
        const int overlayTickStep = juce::jmax(6, (int)juce::roundToInt((float)24 * (16.0f / (float)overlaySteps))); // approx
        for (int t = 0; t <= maxTick; t += overlayTickStep)
        {
            if (rng.nextInt({ 100 }) < 12) // sparse overlay density
            {
                BoomAudioProcessor::Note p;
                // route to a percussion row, prefer perc rows (3..5) if present
                p.row = 4; // perc1 by default (adjust if your row indices differ)
                p.startTick = t;
                p.lengthTicks = juce::jmax(6, overlayTickStep / 2);
                p.velocity = juce::jlimit(1, 127, 30 + rng.nextInt({ 30 }));
                out.add(p);
            }
        }
    }

    //
    // 4) Scatter: percussion "scatter" injection — uses scatter_depth and scatter_density
    //
    if (readParam(apvts, "mode_Scatter", 0.0f) > 0.5f)
    {
        juce::Random rng;
        const int depth = juce::jlimit(0, 100, (int)juce::roundToInt(readParam(apvts, "scatter_depth", 40.0f)));
        const int densityIdx = juce::jlimit(0, 2, (int)juce::roundToInt(readParam(apvts, "scatter_density", 1.0f)));
        int densityPct = (densityIdx == 0) ? 10 : (densityIdx == 1) ? 22 : 38;

        // scatter only on perc rows (rows 3..5) and hats (row 2)
        for (int i = 0; i < out.size(); ++i)
        {
            const auto& n = out.getReference(i);
            if (n.row != 2 && (n.row < 3 || n.row > 6)) continue; // skip non-perc rows
            if (rng.nextInt({ 100 }) > densityPct) continue;

            // add 1..2 scatter notes near n.startTick
            int count = (rng.nextInt({ 100 }) < depth) ? 2 : 1;
            for (int s = 0; s < count; ++s)
            {
                int jitter = rng.nextInt({ 24 }) - 12; // ±12 ticks
                int newStart = juce::jmax(0, n.startTick + jitter);
                bool occupied = false;
                for (const auto& n2 : out) if (std::abs(n2.startTick - newStart) < 6 && n2.row == n.row) { occupied = true; break; }
                if (occupied) continue;

                BoomAudioProcessor::Note sc;
                sc.row = n.row;
                sc.startTick = newStart;
                sc.lengthTicks = juce::jmax(6, rng.nextInt({ 12 }));
                sc.velocity = juce::jlimit(1, 127, n.velocity - 10 + rng.nextInt({ 20 }));
                out.add(sc);
            }
        }
    }

    // After all passes, you can sort events by startTick if you want deterministic ordering
    std::sort(out.begin(), out.end(), [](const BoomAudioProcessor::Note& a, const BoomAudioProcessor::Note& b)
        { return a.startTick < b.startTick; });


    setDrumPattern(out); // setDrumPattern() already calls notifyPatternChanged()
    DBG("generateDrums: final setDrumPattern size=" << getDrumPattern().size());
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
            const int ticksPerStep = boom::grid::ticksPerStepFromPpq(PPQ, 4); // cellsPerBeat = 4 (16th grid)
            int start16 = boom::grid::roundStartTickToStepIndex(n.startTick, ticksPerStep) + off16;
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

    HatStyleRuleSet rules = resolveHatRules(style);

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
    else if (s == "rock") styleSeedAffinity = 0.25f;
    else if (s == "pop") styleSeedAffinity = 0.45f;
    else if (s == "reggaeton") styleSeedAffinity = 0.45f;
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
        if (majority == 2 || coinInt(rules.innerDensityPct))
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
        if (wild || coinInt(20)) // check this
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

    // -------------------- hatPitchPool builder (hi-hat generator) --------------------
// Builds the set of possible hat pitches used below. Keeps drum-mode as default,
// and prepares for a "Tuned" mode if you later want to snap hats to scale.
//
// ---------- improved hatPitchPool builder (paste BEFORE convert stepHits -> MIDI) ----------
// ------------------ hatPitchPool builder (safe, local) ------------------
// Paste this block immediately BEFORE the "convert stepHits -> MIDI note events" section.

    juce::Array<int> hatPitchPool;

    // read tuned setting
    bool hatsTuned = false;
    if (auto* v = apvts.getParameter("hatsTuned"))
    {
        float raw = apvts.getRawParameterValue("hatsTuned")->load();
        if (raw <= 1.5f) raw *= 100.0f; // in case it's 0..1 normalized
        hatsTuned = (raw > 50.0f);
    }

    // build local scalePCs/keyIndex so degreeToPitchGlobal(...) works here
    juce::String keyName = "C";
    if (auto* pKey = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("key")))
        keyName = pKey->getCurrentChoiceName();

    juce::String scaleName = "Chromatic";
    if (auto* pScale = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("scale")))
        scaleName = pScale->getCurrentChoiceName();

    auto itScale = kScales.find(scaleName.trim());
    std::vector<int> scalePCs = (itScale != kScales.end()) ? itScale->second : kScales.at("Chromatic");
    int keyIndex = juce::jmax(0, kKeys.indexOf(keyName.trim().toUpperCase()));

    // Build pool
    if (hatsTuned)
    {
        // Use a set of scale degrees and octave variants to produce variety.
        const int degs[] = { 0, 1, 2, 3, 4, 5 };
        const int baseOct = 4;
        for (int d : degs)
        {
            int p = degreeToPitchGlobal(d, baseOct, scalePCs, keyIndex);
            hatPitchPool.add(juce::jlimit(0, 127, p));
            // add octave variations for texture
            hatPitchPool.add(juce::jlimit(0, 127, p + 12));
            hatPitchPool.add(juce::jlimit(0, 127, p - 12));
        }
        // trim duplicates (simple)
        for (int i = hatPitchPool.size() - 1; i >= 0; --i)
            for (int j = i - 1; j >= 0; --j)
                if (hatPitchPool[i] == hatPitchPool[j])
                    hatPitchPool.remove(i);
    }
    else
    {
        // Non-tuned: build a varied drum hat pool (GM hat + metallics)
        const int drumChoices[] = { 42, 44, 46, 49, 57, 59 };
        for (int d : drumChoices) hatPitchPool.add(d);
        // add some extra offsets so different DAWs may render timbre variance
        hatPitchPool.add(42 + 12);
        hatPitchPool.add(46 + 12);
    }

    // DEBUG: print pool size and a quick sample
    DBG("HiHat pool size = " + juce::String((int)hatPitchPool.size()));
    if (hatPitchPool.size() > 0)
    {
        int idxSample = juce::jmin((int)hatPitchPool.size() - 1, 2);
        DBG("HiHat samples: " + juce::String(hatPitchPool[0]) + ", " + juce::String(hatPitchPool[idxSample]));
    }

    // -----------------------------------------------------------------------


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
        if (allowDotted && coinInt(12)) len = (int)juce::roundToInt(len * 1.5f); // check this

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
        if ((s == "trap" || s == "drill" || s == "r&b") && coinInt((path == 2) ? 50 : 25)) // check this
            doRoll = true;

        if (doRoll)
        {
            bool fast = coinInt(rules.fastRollPct);
            addClosedRoll(tick, len, fast);
        }
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
    // Unambiguous copilot entry log

    juce::ScopedNoDenormals noDenormals;

    // --- DEBUG: confirm processBlock calls and input layout ---
    const int numInCh = buffer.getNumChannels();
    const int numSmps = buffer.getNumSamples();

    // Keep our sample-rate fresh
    lastSampleRate = getSampleRate() > 0.0 ? getSampleRate() : lastSampleRate;

    // --- 2) Poll host for BPM (JUCE 7/8 safe) -------------------------------
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (pos->getBpm().hasValue())
            {
                lastHostBpm.store(*pos->getBpm());
              
            }
        }
    }

    // --- 3) Capture recording (Rhythmimick / Beatbox) -----------------------
    // We record *mono* into captureBuffer by averaging L/R.
    if (aiIsCapturing())
    {
        DBG("processBlock: calling appendCaptureFrom() (aiIsCapturing true)");
        appendCaptureFrom(buffer);
    }
    else
    {

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

    // ----------------- Preview playback support -----------------
    // When the editor requests preview playback (aiPreviewStart -> isPreviewing=true),
    // stream mono captureBuffer into the audio output buffer (mix). Advance previewReadPos.
    if (isPreviewing.load() && captureLengthSamples > 0)
    {
        const int readPos = previewReadPos.load(std::memory_order_relaxed);
        int remaining = captureLengthSamples - readPos;
        if (previewReadPos >= captureLengthSamples)
        {
            isPreviewing.store(false);
            DBG("processBlock: preview reached end -> stopping preview");
        }
        else
        {
            // occasional low-rate log to confirm playhead movement (avoid spamming)
            static int dbgTick = 0; if ((++dbgTick & 127) == 0)
                DBG("processBlock: previewReadPos=" << previewReadPos << " / " << captureLengthSamples);
        }
    }
}

bool BoomAudioProcessor::aiIsCapturing() const noexcept
{
    // Single clear point for "am I capturing" — easier to debug and consistent memory ordering.
    const bool rh = recRh_.load(std::memory_order_acquire);
    const bool bx = recBx_.load(std::memory_order_acquire);
    return rh || bx;
}

// Add these definitions (near the other AI façade methods)
bool BoomAudioProcessor::aiHasCapture() const noexcept
{
    return captureLengthSamples > 0;
}

bool BoomAudioProcessor::aiIsPreviewing() const noexcept
{
    return isPreviewing.load();
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
    DBG(juce::String("appendCaptureFrom entered on this=") + juce::String::toHexString((int64_t)(void*)this)
        + " thread=" + juce::String((int)juce::Thread::getCurrentThreadId())
        + " aiIsCapturing()=" + juce::String(aiIsCapturing() ? 1 : 0)
        + " recRh=" + juce::String((int)recRh_.load()) + " recBx=" + juce::String((int)recBx_.load())
        + " preview=" + juce::String((int)isPreviewing.load()));

    const bool recRh = recRh_.load(std::memory_order_acquire);
    const bool recBx = recBx_.load(std::memory_order_acquire);
    const bool preview = isPreviewing.load(std::memory_order_acquire);
    if (!recRh && !recBx)
    {
        DBG(juce::String("appendCaptureFrom: skipping write because not capturing (recRh=") + juce::String((int)recRh)
            + " recBx=" + juce::String((int)recBx) + ")");
        return;
    }

    if (in.getNumSamples() <= 0)
    {
        DBG("appendCaptureFrom: incoming has zero samples, returning");
        return;
    }

    DBG(juce::String("appendCaptureFrom: incomingSamples=") + juce::String(in.getNumSamples())
        + " chans=" + juce::String(in.getNumChannels())
        + " captureBufSize=" + juce::String(captureBuffer.getNumSamples())
        + " writePosBefore=" + juce::String(captureWritePos)
        + " lengthSamplesBefore=" + juce::String(captureLengthSamples));

    // existing mix-to-mono + write logic unchanged...
    juce::AudioBuffer<float> mono(1, in.getNumSamples());
    mono.clear();
    const int chans = in.getNumChannels();
    for (int ch = 0; ch < chans; ++ch)
        mono.addFrom(0, 0, in, ch, 0, in.getNumSamples(), 1.0f / juce::jmax(1, chans));

    float peak = 0.0f;
    const int chk = juce::jmin(32, mono.getNumSamples());
    const float* mp = mono.getReadPointer(0);
    for (int i = 0; i < chk; ++i) peak = juce::jmax(peak, std::abs(mp[i]));
    DBG(juce::String("appendCaptureFrom: mono first-") + juce::String(chk) + " peak=" + juce::String(peak));

    const int free = captureBuffer.getNumSamples() - captureWritePos;
    const int n = juce::jmin(free, mono.getNumSamples());

    DBG(juce::String("appendCaptureFrom: free=") + juce::String(free) + " willWrite=" + juce::String(n));

    if (n > 0)
        captureBuffer.copyFrom(0, captureWritePos, mono, 0, 0, n);

    captureWritePos += n;
    captureLengthSamples = juce::jmax(captureLengthSamples, captureWritePos);

    DBG(juce::String("appendCaptureFrom: writePosAfter=") + juce::String(captureWritePos)
        + " lengthSamplesAfter=" + juce::String(captureLengthSamples));

    if (captureWritePos >= captureBuffer.getNumSamples())
    {
        DBG("appendCaptureFrom: buffer full; calling aiStopCapture(currentCapture)");
        aiStopCapture(currentCapture);
    }

    DBG(juce::String("appendCaptureFrom: wrote=") + juce::String(n) + " newWritePos=" + juce::String(captureWritePos) + " length=" + juce::String(captureLengthSamples));
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
            return toTick16(step); // use helper (step -> ticks) instead of hard-coded 24 multiplier
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
    DBG(juce::String("aiStartCapture() called on this=") + juce::String::toHexString((int64_t)(void*)this)
        + " thread=" + juce::String((int)juce::Thread::getCurrentThreadId())
        + " src=" + juce::String((int)src));

    // Stop any previous capture first
    aiStopCapture();

    currentCapture = src;

    // Ensure buffer exists
    lastSampleRate = getSampleRate() > 0.0 ? getSampleRate() : lastSampleRate;
    ensureCaptureCapacitySeconds(65.0);
    captureBuffer.clear();
    captureWritePos = 0;
    captureLengthSamples = 0;

    // Set the flags with release semantics and barrier so audio thread sees update quickly
    if (src == CaptureSource::Loopback)
    {
        recRh_.store(true, std::memory_order_release);
        recBx_.store(false, std::memory_order_release);
    }
    else
    {
        recRh_.store(false, std::memory_order_release);
        recBx_.store(true, std::memory_order_release);
    }

    // Stronger ordering visibility
    std::atomic_thread_fence(std::memory_order_seq_cst);

    DBG(juce::String("aiStartCapture: after store this=") + juce::String::toHexString((int64_t)(void*)this)
        + " recRh=" + juce::String((int)recRh_.load())
        + " recBx=" + juce::String((int)recBx_.load())
        + " preview=" + juce::String((int)isPreviewing.load())
        + " bufferSamples=" + juce::String(captureBuffer.getNumSamples()));

    juce::Logger::writeToLog("aiStartCapture: isCapturing now true on this=" + juce::String::toHexString((int64_t)(void*)this));

    if (auto* ed = getActiveEditor()) ed->repaint();
}

// --- aiStopCapture overload that accepts a source (existing implementation, now clears rec flags) ---
void BoomAudioProcessor::aiStopCapture(CaptureSource src)
{
    DBG(juce::String("aiStopCapture(src) called on this=") + juce::String::toHexString((int64_t)(void*)this)
        + " thread=" + juce::String((int)juce::Thread::getCurrentThreadId())
        + " src=" + juce::String((int)src)
        + " before recRh=" + juce::String((int)recRh_.load())
        + " recBx=" + juce::String((int)recBx_.load()));

    recRh_.store(false, std::memory_order_release);
    recBx_.store(false, std::memory_order_release);

    // Memory fence for visibility
    std::atomic_thread_fence(std::memory_order_seq_cst);

    juce::Logger::writeToLog("aiStopCapture: stopped capturing on this=" + juce::String::toHexString((int64_t)(void*)this));
    DBG(juce::String("aiStopCapture: after store recRh=") + juce::String((int)recRh_.load())
        + " recBx=" + juce::String((int)recBx_.load())
        + " writePos=" + juce::String(captureWritePos)
        + " length=" + juce::String(captureLengthSamples));

    if (auto* ed = getActiveEditor()) ed->repaint();
}

// --- aiStopCapture no-arg wrapper (some code calls aiStopCapture() without args) ---
void BoomAudioProcessor::aiStopCapture()
{
    aiStopCapture(currentCapture);
}

// === Simple façade methods used by AIToolsWindow ===
void BoomAudioProcessor::ai_endRhRecord()
{
    aiStopCapture(CaptureSource::Loopback);
}
bool BoomAudioProcessor::ai_isRhRecording() const noexcept
{
    return recRh_.load();
}

void BoomAudioProcessor::ai_endBxRecord()
{
    aiStopCapture(CaptureSource::Microphone);
}
bool BoomAudioProcessor::ai_isBxRecording() const noexcept
{
    return recBx_.load();
}


void BoomAudioProcessor::aiPreviewStart()
{
    DBG("aiPreviewStart() called. captureLengthSamples=" << captureLengthSamples << " lastSampleRate=" << lastSampleRate);
    if (captureLengthSamples <= 0)
    {
        DBG("aiPreviewStart: no captured samples - ignoring");
        return;
    }
    isPreviewing.store(true);
    previewReadPos = 0;
}

void BoomAudioProcessor::aiPreviewStop()
{
    DBG("aiPreviewStop() called. wasPreviewing=" << (int)isPreviewing.load());
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
    if (lastSampleRate <= 0.0) return 0.0;
    const int pos = previewReadPos.load();
    const int clamped = juce::jlimit(0, captureLengthSamples, pos);
    return static_cast<double>(clamped) / lastSampleRate;
}

void BoomAudioProcessor::aiSeekToSeconds(double sec) noexcept
{
    DBG("aiSeekToSeconds() called. requestedSec=" << sec << " captureLenSec=" << getCaptureLengthSeconds());
    if (lastSampleRate <= 0.0 || captureLengthSamples <= 0) return;
    const int target = (int)juce::jlimit(0.0, getCaptureLengthSeconds(), sec) * (int)lastSampleRate;
    previewReadPos = juce::jlimit(0, captureLengthSamples, target);
    capturePlayheadSamples.store(previewReadPos);
    DBG("aiSeekToSeconds -> previewReadPos=" << previewReadPos);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BoomAudioProcessor();
}