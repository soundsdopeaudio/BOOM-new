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
#include "DrumStyleEnforcer.h"
#include "RollStyleProfiles.h"
#include "RollStyleProfileResolver.h"
#include "HatStyleProfileResolver.h"
#include "RollStyleRuleSet.h"
#include "BassStyleProfiles.h"


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
    
    const int numerator = getTimeSigNumerator();
    const int denominator = getTimeSigDenominator();
    
    boom::drums::DrumStyleSpec specA = boom::drums::getSpecForTimeSig(
        styleA,
        numerator,
        denominator,
        /*seed*/ -1
    );

    boom::drums::DrumStyleSpec specB = boom::drums::getSpecForTimeSig(
        styleB,
        numerator,
        denominator,
        /*seed*/ -1
    );
    boom::drums::generate(specA, bars, restPct, dottedPct, tripletPct, swingPct, /*seed*/ -1, numerator, denominator, patA);
    boom::drums::generate(specB, bars, restPct, dottedPct, tripletPct, swingPct, /*seed*/ -1, numerator, denominator, patB);

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

    boom::drums::DrumStyleSpec spec = boom::drums::getSpecForTimeSig(
        baseStyle,
        getTimeSigNumerator(),
        getTimeSigDenominator(),
        /*seed*/ -1
    );
    boom::drums::DrumPattern pat;
    const int numerator = getTimeSigNumerator();
    const int denominator = getTimeSigDenominator();
    boom::drums::generate(spec, bars, restPct, dottedPct, tripletPct, swingPct, /*seed*/ -1, numerator, denominator, pat);

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


    boom::drums::DrumStyleSpec spec = boom::drums::getSpecForTimeSig(
        style,
        getTimeSigNumerator(),
        getTimeSigDenominator(),
        /*seed*/ -1
    );
    boom::drums::DrumPattern pat;
    const int numerator = getTimeSigNumerator();
    const int denominator = getTimeSigDenominator();
    boom::drums::generate(spec, bars, restPct, dottedPct, tripletPct, swingPct, /*seed*/ -1, numerator, denominator, pat);

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
    juce::String tsParam = "4/4";
    if (auto* tsChoice = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("timeSig")))
        tsParam = tsChoice->getCurrentChoiceName();
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
        // Note: use the correct param names "useTriplets" and "useDotted" (not "allow...")
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
    // ============================================================
    // BASS (NON-808) GENERATOR
    // - Uses the UI controls you already have:
    //   bars, style, time signature, rest density, swing, humanize timing/velocity,
    //   dotted/triplet toggles (already gated in the editor via dottedPct/tripletPct)
    // - Produces bassline rhythms appropriate for each style.
    // ============================================================

    // ---- Clamp user inputs ----
    bars = juce::jlimit(1, 8, bars);
    restPct = juce::jlimit(0, 100, restPct);
    dottedPct = juce::jlimit(0, 100, dottedPct);
    tripletPct = juce::jlimit(0, 100, tripletPct);
    swingPct = juce::jlimit(0, 100, swingPct);

    const int densityPercent = juce::jlimit(0, 100, 100 - restPct);
    const bool allowDotted = (dottedPct > 0);
    const bool allowTriplets = (tripletPct > 0);

    // ---- Read humanize sliders (0..100; supports 0..1 storage) ----
    auto clampPct = [](float v) -> int {
        if (v > 1.5f) return juce::jlimit(0, 100, (int)juce::roundToInt(v));
        return juce::jlimit(0, 100, (int)juce::roundToInt(v * 100.0f));
        };

    int humanizeTimingPct = 0;
    if (auto* ht = apvts.getRawParameterValue("humanizeTiming"))
        humanizeTimingPct = clampPct(ht->load());

    int humanizeVelocityPct = 0;
    if (auto* hv = apvts.getRawParameterValue("humanizeVelocity"))
        humanizeVelocityPct = clampPct(hv->load());

    // ---- Time signature from APVTS Choice "timeSig" (NOT apvts.state property) ----
    juce::String tsParam = "4/4";
    if (auto* tsChoice = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("timeSig")))
        tsParam = tsChoice->getCurrentChoiceName();

    auto tsParts = juce::StringArray::fromTokens(tsParam, "/", "");
    const int tsNum = juce::jmax(1, tsParts.size() >= 1 ? tsParts[0].getIntValue() : 4);
    const int tsDen = juce::jmax(1, tsParts.size() >= 2 ? tsParts[1].getIntValue() : 4);

    auto stepsPerBarFor = [&](int num, int den)->int
        {
            // 4/4 -> 16 steps, 3/4 -> 12, 6/8 -> 12, 7/8 -> 14, etc.
            if (den == 4)  return num * 4;
            if (den == 8)  return num * 2;
            if (den == 16) return num;
            return num * 4;
        };

    const int stepsPerBar = stepsPerBarFor(tsNum, tsDen);
    const int totalSteps = stepsPerBar * bars;

    // We generate on a 16th-based step grid where 1 step = 1/16 note in 4/4.
    const int ppq = PPQ;                          // project PPQ (you use 96 elsewhere)
    const int stepTicks = ppq / 4;                // 96/4 = 24 ticks per 16th
    const int barTicks = stepsPerBar * stepTicks; // bar length in ticks
    juce::ignoreUnused(barTicks);

    // ---- Key/scale (same tables you already use for 808/Bass) ----
    juce::String keyName = "C";
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("key")))
        keyName = p->getCurrentChoiceName();

    juce::String scaleName = "Major";
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("scale")))
        scaleName = p->getCurrentChoiceName();

    const int keyIndex = juce::jmax(0, kKeys.indexOf(keyName.trim().toUpperCase()));
    const auto itScale = kScales.find(scaleName.trim());
    const auto& scalePCs = (itScale != kScales.end()) ? itScale->second : kScales.at("Chromatic");

    auto wrap12Local = [](int v) { v %= 12; if (v < 0) v += 12; return v; };

    auto degreeToPitch = [&](int degree, int oct)->int
        {
            if (scalePCs.empty())
                return juce::jlimit(0, 127, oct * 12 + wrap12Local(keyIndex));

            const int sz = (int)scalePCs.size();
            int di = degree % sz;
            if (di < 0) di += sz;
            const int pc = scalePCs[di];
            return juce::jlimit(0, 127, oct * 12 + wrap12Local(keyIndex + pc));
        };

    // ---- RNG (deterministic if seed provided) ----
    const auto now32 = juce::Time::getMillisecondCounter();
    const auto ticks64 = (std::uint64_t)juce::Time::getHighResolutionTicks();
    const auto nonce = genNonce_.fetch_add(1, std::memory_order_relaxed) + 1;
    const std::uint64_t mix = (std::uint64_t)now32 ^ ticks64 ^ (std::uint64_t)nonce;
    const int rngSeed = (seed == -1) ? (int)(mix & 0x7fffffff) : seed;
    juce::Random rng(rngSeed);

    auto pct = [&](int p)->bool { return rng.nextInt({ 100 }) < juce::jlimit(0, 100, p); };

    // ---- Style normalization ----
    const juce::String styleLower = styleName.trim().toLowerCase();

    // ---- Style rhythm spec (16-slot “feel map” resampled to any TS) ----
    BassStyleSpec spec = getBassStyleSpec(styleLower);

    // Expand style coverage (some UI strings have spaces/casing)
    if (styleLower == "hip-hop")       spec = getBassStyleSpec("hip hop");
    if (styleLower == "west coast")    spec = getBassStyleSpec("wxstie");
    if (styleLower == "rnb")           spec = getBassStyleSpec("r&b");
    if (styleLower == "pop")           spec = getBassStyleSpec("pop-ish"); // default spec

    // ---- Style-specific musical intent ----
    int baseOct = 2 + octave; // C2-ish default
    int minLenSteps = 2, maxLenSteps = 8;
    int minGapSteps = 0;
    int baseVel = 102;
    float extraSplit32Prob = 0.0f; // short follow-up “bop” after an onset

    if (styleLower == "trap")
    {
        baseOct = 2 + octave;
        minLenSteps = 3; maxLenSteps = 10;
        minGapSteps = 0;
        baseVel = 110;
        extraSplit32Prob = 0.22f;
    }
    else if (styleLower == "drill")
    {
        baseOct = 2 + octave;
        minLenSteps = 2; maxLenSteps = 8;
        minGapSteps = 0;
        baseVel = 112;
        extraSplit32Prob = 0.28f;
    }
    else if (styleLower == "wxstie")
    {
        baseOct = 2 + octave;
        minLenSteps = 4; maxLenSteps = 12;
        minGapSteps = 1;
        baseVel = 108;
        extraSplit32Prob = 0.08f;
    }
    else if (styleLower == "hip hop" || styleLower == "hip-hop" || styleLower == "hiphop")
    {
        baseOct = 2 + octave;
        minLenSteps = 3; maxLenSteps = 10;
        minGapSteps = 0;
        baseVel = 104;
        extraSplit32Prob = 0.10f;
    }
    else if (styleLower == "rock")
    {
        baseOct = 2 + octave;
        minLenSteps = 4; maxLenSteps = 8;
        minGapSteps = 0;
        baseVel = 100;
        extraSplit32Prob = 0.04f;
    }
    else if (styleLower == "pop")
    {
        baseOct = 2 + octave;
        minLenSteps = 3; maxLenSteps = 8;
        minGapSteps = 0;
        baseVel = 98;
        extraSplit32Prob = 0.06f;
    }
    else if (styleLower == "r&b" || styleLower == "rnb")
    {
        baseOct = 2 + octave;
        minLenSteps = 3; maxLenSteps = 10;
        minGapSteps = 0;
        baseVel = 104;
        extraSplit32Prob = 0.12f;
    }
    else if (styleLower == "edm")
    {
        baseOct = 2 + octave;
        minLenSteps = 2; maxLenSteps = 6;
        minGapSteps = 0;
        baseVel = 112;
        extraSplit32Prob = 0.10f;
    }
    else if (styleLower == "reggaeton")
    {
        baseOct = 2 + octave;
        minLenSteps = 2; maxLenSteps = 8;
        minGapSteps = 0;
        baseVel = 110;
        extraSplit32Prob = 0.08f;
    }

    baseOct = juce::jlimit(0, 10, baseOct);

    // ---- Content buffers ----
    auto mp = getMelodicPattern();
    mp.clear();

    TickGuard guard;
    guard.bucketSize = 12;
    const int channelBass = 1;

    // ---- Helpers ----
    auto weightForStepInBar = [&](int stepInBar)->int
        {
            const int idx16 = juce::jlimit(0, 15, (stepInBar * 16) / juce::jmax(1, stepsPerBar));
            int w = spec.weight16[idx16];

            if (spec.syncBias > 0.001f && (idx16 & 1))
                w = juce::jmin(15, w + (int)juce::roundToInt(3.0f * spec.syncBias));
            if (spec.syncBias < -0.001f && ((idx16 & 1) == 0))
                w = juce::jmin(15, w + (int)juce::roundToInt(-3.0f * spec.syncBias));

            return juce::jmax(0, w);
        };

    auto computeSwingOffsetTicks = [&](int step)->int
        {
            if (swingPct <= 0) return 0;

            const int spb = juce::jmax(1, stepsPerBar / tsNum);
            if (spb < 2) return 0;

            const int stepInBeat = step % spb;

            if (stepInBeat == (spb / 2))
            {
                const float amt01 = (float)swingPct / 100.0f;
                return (int)juce::roundToInt((float)stepTicks * 0.5f * amt01);
            }
            return 0;
        };

    auto applyHumanize = [&](int& startTick, int& vel)
        {
            if (humanizeTimingPct > 0)
            {
                const float amt01 = (float)humanizeTimingPct / 100.0f;
                const int maxJ = (int)juce::roundToInt((float)stepTicks * 0.35f * amt01);
                if (maxJ > 0)
                    startTick = juce::jmax(0, startTick + rng.nextInt({ -maxJ, maxJ + 1 }));
            }

            if (humanizeVelocityPct > 0)
            {
                const float amt01 = (float)humanizeVelocityPct / 100.0f;
                const int spread = (int)juce::roundToInt(24.0f * amt01);
                if (spread > 0)
                    vel = juce::jlimit(1, 127, vel + rng.nextInt({ -spread, spread + 1 }));
            }
        };

    auto maybeApplyDottedTripletLength = [&](int baseLenTicks)->int
        {
            int len = baseLenTicks;

            if (allowTriplets && pct(tripletPct))
                len = juce::jmax(1, (int)juce::roundToInt((double)len * (2.0 / 3.0)));

            if (allowDotted && pct(dottedPct))
                len = juce::jmax(1, (int)juce::roundToInt((double)len * 1.5));

            return len;
        };

    // ---- Melodic movement ----
    int currentDegree = 0;
    int currentOct = baseOct;

    auto chooseNextDegreeDelta = [&]()->int
        {
            int wStay = 55, wStep = 25, wFifth = 15, wOct = 5;

            if (styleLower == "trap") { wStay = 55; wStep = 20; wFifth = 20; wOct = 5; }
            else if (styleLower == "drill") { wStay = 45; wStep = 30; wFifth = 20; wOct = 5; }
            else if (styleLower == "wxstie") { wStay = 60; wStep = 28; wFifth = 10; wOct = 2; }
            else if (styleLower == "r&b" || styleLower == "rnb") { wStay = 45; wStep = 35; wFifth = 15; wOct = 5; }
            else if (styleLower == "hip hop" || styleLower == "hip-hop" || styleLower == "hiphop") { wStay = 50; wStep = 30; wFifth = 15; wOct = 5; }
            else if (styleLower == "rock" || styleLower == "pop") { wStay = 60; wStep = 20; wFifth = 18; wOct = 2; }
            else if (styleLower == "edm") { wStay = 65; wStep = 18; wFifth = 15; wOct = 2; }
            else if (styleLower == "reggaeton") { wStay = 55; wStep = 25; wFifth = 18; wOct = 2; }

            const int total = juce::jmax(1, wStay + wStep + wFifth + wOct);
            const int r = rng.nextInt({ total });
            int acc = 0;

            acc += wStay;  if (r < acc) return 0;
            acc += wStep;  if (r < acc) return (rng.nextBool() ? +1 : -1);
            acc += wFifth; if (r < acc) return (rng.nextBool() ? +4 : -4);

            return (rng.nextBool() ? +7 : -7);
        };

    auto placeBassNote = [&](int step, int lenSteps, int vel)
        {
            int startTick = step * stepTicks;

            startTick += computeSwingOffsetTicks(step);
            applyHumanize(startTick, vel);

            const int pitch = degreeToPitch(currentDegree, currentOct);

            int lenTicks = juce::jmax(1, lenSteps * stepTicks);
            lenTicks = maybeApplyDottedTripletLength(lenTicks);

            const int maxTick = totalSteps * stepTicks;
            if (startTick >= maxTick) return;
            lenTicks = juce::jmax(1, juce::jmin(lenTicks, maxTick - startTick));

            if (!placeNoteUnique(mp, guard, startTick, lenTicks, pitch, vel, channelBass))
                placeNoteUnique(mp, guard, juce::jmin(maxTick - 1, startTick + 6), lenTicks, pitch, vel, channelBass);

            if (extraSplit32Prob > 0.001f && rng.nextFloat() < extraSplit32Prob)
            {
                const int splitStart = startTick + (stepTicks / 2);
                if (splitStart < maxTick - 3)
                {
                    int v2 = juce::jlimit(1, 127, vel - 14);
                    int st2 = splitStart;
                    applyHumanize(st2, v2);

                    int len2 = juce::jmax(3, stepTicks / 2);
                    len2 = juce::jmin(len2, maxTick - st2);

                    int pitch2 = pitch;
                    if (pct(20)) pitch2 = juce::jlimit(0, 127, pitch + 12);

                    placeNoteUnique(mp, guard, st2, len2, pitch2, v2, channelBass);
                }
            }

            if ((styleLower == "drill") && allowTriplets && pct(juce::jmin(65, tripletPct)))
            {
                const int ratchetStart = startTick + (int)juce::roundToInt((double)stepTicks * (2.0 / 3.0));
                if (ratchetStart < maxTick - 3)
                {
                    int v3 = juce::jlimit(1, 127, vel - 18);
                    int st3 = ratchetStart;
                    applyHumanize(st3, v3);

                    int len3 = juce::jmax(3, (int)juce::roundToInt((double)stepTicks * (2.0 / 3.0)));
                    len3 = juce::jmin(len3, maxTick - st3);

                    placeNoteUnique(mp, guard, st3, len3, pitch, v3, channelBass);
                }
            }
        };

    // ---- Main generation ----
    int step = 0;

    // First downbeat anchor (unless density is extremely low)
    if (densityPercent > 5)
    {
        const int span = juce::jmax(1, maxLenSteps - minLenSteps + 1);
        const int lenSteps = juce::jlimit(1, 12, minLenSteps + rng.nextInt({ span }));
        placeBassNote(0, lenSteps, baseVel);
        step = juce::jmax(1, minGapSteps + (lenSteps / 2));
        if (pct(25)) currentDegree += chooseNextDegreeDelta();
    }

    for (; step < totalSteps; )
    {
        const int stepInBar = step % stepsPerBar;
        const int w = weightForStepInBar(stepInBar);

        const float density01 = (float)densityPercent / 100.0f;
        const float w01 = (float)juce::jlimit(0, 15, w) / 15.0f;
        float pHit = density01 * spec.baseDensity * (0.35f + 0.65f * w01);

        if (styleLower == "wxstie") pHit *= 0.70f;
        if (styleLower == "rock" || styleLower == "pop") pHit *= 1.10f;
        if (styleLower == "reggaeton" && (stepInBar == 0 || stepInBar == (stepsPerBar / 2))) pHit *= 1.15f;

        pHit = juce::jlimit(0.0f, 1.0f, pHit);

        if (rng.nextFloat() >= pHit)
        {
            ++step;
            continue;
        }

        int lenSteps = minLenSteps;
        const int span = juce::jmax(1, maxLenSteps - minLenSteps + 1);
        lenSteps = minLenSteps + rng.nextInt({ span });

        if ((styleLower == "wxstie" || styleLower == "r&b" || styleLower == "rnb" || styleLower.startsWith("hip")) && pct(40))
            lenSteps = juce::jmin(maxLenSteps, lenSteps + rng.nextInt({ 3 }));

        if (styleLower == "edm" && pct(35))
            lenSteps = juce::jmax(1, lenSteps - 1);

        if (pct(55))
        {
            const int stepsLeftInBar = stepsPerBar - stepInBar;
            lenSteps = juce::jmin(lenSteps, juce::jmax(1, stepsLeftInBar));
        }

        placeBassNote(step, lenSteps, baseVel + rng.nextInt({ 10 }));

        if (pct(60))
            currentDegree += chooseNextDegreeDelta();

        if (pct(8))
            currentOct = juce::jlimit(0, 10, currentOct + (rng.nextBool() ? +1 : -1));

        step += juce::jmax(1, minGapSteps + juce::jmax(1, lenSteps / 2));
    }

    // Safety: if everything got rested out, force a root note
    if (mp.size() == 0)
    {
        currentDegree = 0;
        currentOct = baseOct;
        placeBassNote(0, juce::jmax(2, minLenSteps), baseVel);
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
    juce::MidiMessageSequence seq;

    // -----------------------------
    // Time / safety
    // -----------------------------
    bars = juce::jlimit(1, 8, bars);
    tsNum = juce::jmax(1, tsNum);
    tsDen = juce::jmax(1, tsDen);

    const int ppq = kPPQ; // 96
    const int ticksPerBeat = ppq;                    // quarter note
    const int ticksPerBar = (ppq * 4 * tsNum) / tsDen; // time-signature aware

    const int totalTicks = bars * ticksPerBar;

    // Seed / RNG
    juce::Random rng(seed == -1 ? (int)juce::Time::getMillisecondCounter() : seed);

    auto chance = [&](int pct) -> bool
        {
            pct = juce::jlimit(0, 100, pct);
            return rng.nextInt(100) < pct;
        };

    auto irand = [&](int a, int b) -> int
        {
            if (b < a) std::swap(a, b);
            return a + rng.nextInt((b - a) + 1);
        };

    auto clampTick = [&](int t) -> int
        {
            return juce::jlimit(0, juce::jmax(0, totalTicks - 1), t);
        };

    // -----------------------------
    // Tuned handling (Rolls window)
    // -----------------------------
    const bool rollsTuned = readParam(apvts, "rollsTuned", 0.0f) > 0.5f;

    juce::String keyName = "C";
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("key")))
        keyName = p->getCurrentChoiceName();

    juce::String scaleName = "Chromatic";
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("scale")))
        scaleName = p->getCurrentChoiceName();

    // If tuned is ON but key/scale are somehow missing/invalid, we still must NOT fall back to boring root+neighbor.
    // Per your instruction: if they do NOT select a key/scale, pick a note between C3..C4.
    const bool tunedUsable =
        rollsTuned &&
        (kKeys.indexOf(keyName.trim().toUpperCase()) >= 0) &&
        (kScales.find(scaleName.trim()) != kScales.end());

    const int baseLoosePitch = irand(48, 60); // C3..C4 when not tuned (your instruction)

    auto pickScaleDegree = [&](int loDeg, int hiDeg) -> int
        {
            if (!tunedUsable)
                return 0;
            const auto& pcs = kScales.find(scaleName.trim())->second;
            if (pcs.empty()) return 0;
            loDeg = juce::jmax(0, loDeg);
            hiDeg = juce::jmax(loDeg, hiDeg);
            return irand(loDeg, hiDeg);
        };

    auto pickPitch = [&](int degree, int octave) -> int
        {
            if (!tunedUsable)
            {
                // Not tuned: random around C3..C4, plus small drift so it’s not static
                return juce::jlimit(0, 127, baseLoosePitch + degree);
            }
            return degreeToPitch(keyName, scaleName, degree, octave);
        };

    // -----------------------------
    // Style + “type of generation” selection
    // -----------------------------
    const juce::String s = style.trim().toLowerCase();

    // These implement your “1 out of 4”, “1 out of 6”, etc rules.
    // We do not literally force exact ratios per session; we pick a mode with those approximate chances.
    enum class Mode { Steady, ChoppyGaps, SyncopatedRisky, RollHeavyRisky };
    Mode mode = Mode::Steady;

    // Defaults
    int baseGapPct = (int)juce::jlimit(0.f, 100.f, readParam(apvts, "restDensity", 10.0f)); // if you drive this from UI
    int extraGapPct = 0;

    if (chance(17))          mode = Mode::SyncopatedRisky; // ~1/6
    if (chance(12))          mode = Mode::RollHeavyRisky;  // ~1/8 (overrides sometimes)
    else if (chance(25))     mode = Mode::ChoppyGaps;      // ~1/4

    // wxstie: make gaps more common as you requested
    if (s.contains("wxstie"))
    {
        if (mode == Mode::Steady && chance(70)) mode = Mode::ChoppyGaps;
        extraGapPct += 18;
    }

    // hip-hop: keep simpler; reduce roll-heavy frequency
    if (s.contains("hip hop") || s == "hiphop" || s.contains("hiphop"))
    {
        if (mode == Mode::RollHeavyRisky && chance(75))
            mode = Mode::ChoppyGaps;
        extraGapPct += 8;
    }

    // -----------------------------
    // Majority note length selection (time-signature aware)
    // -----------------------------
    auto q = ticksPerBeat;        // quarter
    auto e = ticksPerBeat / 2;    // eighth
    auto s16 = ticksPerBeat / 4;   // 16th
    auto s32 = juce::jmax(1, ticksPerBeat / 8); // 32nd

    auto qT = (ticksPerBeat * 2) / 3;    // quarter-triplet
    auto eT = (ticksPerBeat) / 3;        // 8th-triplet
    auto s16T = (ticksPerBeat) / 6;       // 16th-triplet
    auto s32T = juce::jmax(1, ticksPerBeat) / 12; // 32nd-triplet

    auto pickMajorLen = [&]() -> int
        {
            // TRAP: mostly 8ths/16ths, can be 8T/16T, rare 32nds
            if (s.contains("trap"))
            {
                int r = rng.nextInt(100);
                if (allowTriplets && r < 18)  return (r < 10 ? eT : s16T);
                if (r < 70)                   return e;
                if (r < 95)                   return s16;
                return s32;
            }

            // DRILL: prioritize triplets (8T / 16T / rare qT)
            if (s.contains("drill"))
            {
                int r = rng.nextInt(100);
                if (allowTriplets)
                {
                    if (r < 10)  return qT;
                    if (r < 70)  return eT;
                    if (r < 92)  return s16T;
                }
                // fallback
                if (r < 70) return e;
                return s16;
            }

            // HIPHOP: mostly 8ths, some quarters, rare 16ths
            if (s.contains("hip hop") || s == "hiphop" || s.contains("hiphop"))
            {
                int r = rng.nextInt(100);
                if (r < 65) return e;
                if (r < 92) return q;
                return s16;
            }

            // EDM: 16ths and 8ths (build feel)
            if (s.contains("edm"))
            {
                int r = rng.nextInt(100);
                if (r < 60) return s16;
                if (r < 92) return e;
                return s32;
            }

            // WXSTIE: like trap but with more space (we handle with gaps, not slower base)
            if (s.contains("wxstie"))
            {
                int r = rng.nextInt(100);
                if (allowTriplets && r < 14)  return eT;
                if (r < 70)                   return e;
                return s16;
            }

            // Others: safe general roll generator
            int r = rng.nextInt(100);
            if (allowTriplets && r < 10) return eT;
            if (r < 70) return e;
            if (r < 92) return s16;
            return q;
        };

    const int majorityLen = juce::jmax(1, pickMajorLen());

    // -----------------------------
    // Pitch motion plan (your “asc/desc most of the time”)
    // -----------------------------
    const bool preferAscDesc =
        s.contains("trap") || s.contains("drill") || s.contains("edm") || s.contains("wxstie") ||
        ((s.contains("hip hop") || s.contains("hiphop")) && chance(60));

    int motionDir = 0; // -1, 0, +1
    if (preferAscDesc)
        motionDir = chance(50) ? 1 : -1;

    // Start degree range wide enough to avoid “root + neighbor” boredom.
    int curDeg = tunedUsable ? pickScaleDegree(0, 6) : 0;
    int curOct = 4; // centered
    if (!tunedUsable)
        curOct = (chance(50) ? 3 : 4);

    // Change note every bar or half-bar (your instruction)
    const int changeEveryTicks = chance(55) ? (ticksPerBar / 2) : ticksPerBar;

    // -----------------------------
    // Roll injection helpers
    // -----------------------------
    auto addRollBurst = [&](int startTick, int burstTicks, int basePitch)
        {
            // Roll note-length selection:
            // trap wants 8/16T or 32nds; drill wants triplets; occasional rare fast.
            juce::Array<int> subs;
            subs.add(s32);
            if (allowTriplets)
            {
                subs.add(s16T);
                subs.add(s32T);
            }
            // Rare faster
            if (chance(6))
            {
                subs.add(juce::jmax(1, s32 / 2)); // 64-ish
                if (allowTriplets) subs.add(juce::jmax(1, s32T / 2));
            }

            int sub = subs[rng.nextInt(subs.size())];
            sub = juce::jmax(1, sub);

            int rollTypePick = rng.nextInt(100);
            int dir = 0; // stationary default
            if (rollTypePick < 34) dir = 1;        // ascend
            else if (rollTypePick < 68) dir = -1;  // descend
            else dir = 0;                            // stationary

            int steps = juce::jlimit(3, 24, burstTicks / sub);
            int tick = startTick;

            int degStep = chance(70) ? 1 : 2;

            for (int i = 0; i < steps; ++i)
            {
                if (tick >= totalTicks) break;

                int p = basePitch;
                if (dir != 0)
                {
                    if (tunedUsable)
                    {
                        // move by scale degrees
                        int d = curDeg + dir * (i * degStep);
                        p = pickPitch(d, curOct);
                    }
                    else
                    {
                        // move by semitone-ish steps within a pleasing range
                        p = juce::jlimit(0, 127, basePitch + dir * (i * irand(1, 3)));
                    }
                }

                int vel = juce::jlimit(1, 127, irand(70, 112) - (i * irand(0, 3)));
                addNote(seq, tick, sub, p, vel, 1);
                tick += sub;
            }
        };

    // -----------------------------
    // Main event loop
    // -----------------------------
    int t = 0;
    int nextChange = changeEveryTicks;

    while (t < totalTicks)
    {
        // decide pitch update at bar/half-bar boundaries
        if (t >= nextChange)
        {
            if (tunedUsable)
            {
                // ascend/descend most of the time for trap/drill etc
                if (motionDir != 0)
                    curDeg += motionDir * irand(1, 2);
                else
                    curDeg = pickScaleDegree(0, 10);

                // keep in a usable range
                curDeg = juce::jlimit(-3, 18, curDeg);

                // occasionally shift octave (prevents “two-note boredom”)
                if (chance(20))
                    curOct = juce::jlimit(3, 5, curOct + (chance(50) ? 1 : -1));
            }
            else
            {
                // not tuned: drift pitch slightly
                if (chance(70))
                    curDeg += (chance(50) ? 1 : -1) * irand(1, 2);
                curDeg = juce::jlimit(-8, 8, curDeg);
            }

            nextChange += changeEveryTicks;
        }

        int pitchNow = tunedUsable ? pickPitch(curDeg, curOct) : juce::jlimit(0, 127, baseLoosePitch + curDeg);

        // gap logic (mode + restDensity)
        int gapPct = juce::jlimit(0, 100, baseGapPct + extraGapPct);
        if (mode == Mode::Steady) gapPct = juce::jlimit(0, 100, gapPct - 12);
        if (mode == Mode::ChoppyGaps) gapPct = juce::jlimit(0, 100, gapPct + 12);
        if (mode == Mode::SyncopatedRisky) gapPct = juce::jlimit(0, 100, gapPct + 18);
        if (mode == Mode::RollHeavyRisky) gapPct = juce::jlimit(0, 100, gapPct + 10);

        // wxstie: “should never be steady most of the time”
        if (s.contains("wxstie"))
            gapPct = juce::jlimit(0, 100, gapPct + 18);

        // Apply gap
        if (chance(gapPct))
        {
            // occasionally add a roll even during a gap (risky syncopation)
            // IMPORTANT: if we start a roll, we must advance time by the roll length
            // so no other notes land underneath it.
            if ((mode == Mode::SyncopatedRisky || mode == Mode::RollHeavyRisky) && chance(10))
            {
                int burst = juce::jmin(ticksPerBeat, juce::jmax(majorityLen * 2, ticksPerBeat / 2));
                burst = juce::jmax(majorityLen, burst);
                addRollBurst(t, burst, pitchNow);
                t += burst;
                continue;
            }

            t += majorityLen;
            continue;
        }

        // dotted handling (only if allowed & you asked for it)
        int len = majorityLen;
        if (allowDotted && chance(10))
            len = (len * 3) / 2;

        // “risky” modes: occasionally offset by a small amount (human syncopation feel)
        int start = t;
        if (mode == Mode::SyncopatedRisky && chance(22))
        {
            int offset = juce::jmax(1, majorityLen / 4);
            start = clampTick(t + (chance(50) ? offset : -offset));
        }

        // roll chance rules (style-aware)
        int rollPct = 0;
        if (s.contains("trap")) rollPct = 18;
        else if (s.contains("drill")) rollPct = 24;
        else if (s.contains("hip hop") || s.contains("hiphop")) rollPct = 10;
        else if (s.contains("edm")) rollPct = 20;
        else if (s.contains("wxstie")) rollPct = 16;
        else rollPct = 14;

        if (mode == Mode::RollHeavyRisky) rollPct += 18;
        if (mode == Mode::SyncopatedRisky) rollPct += 10;

        // IMPORTANT: one note at a time.
        // If a roll triggers, we DO NOT place the main note underneath it.
        // We play the roll uninterrupted, advance time by its duration,
        // then resume the pattern after the roll ends.
        if (chance(rollPct))
        {
            int burst = ticksPerBeat / 2; // ~8th
            if (chance(35)) burst = ticksPerBeat; // ~quarter
            burst = juce::jmax(majorityLen, burst);

            addRollBurst(start, burst, pitchNow);

            t += burst;
            continue;
        }

        // No roll: place the single main note
        int vel = irand(75, 115);
        addNote(seq, start, juce::jmax(1, len), pitchNow, vel, 1);

        // Move forward normally
        t += majorityLen;
    }

    // Safety fallback: if somehow empty, drop one note
    if (seq.getNumEvents() == 0)
    {
        int p = tunedUsable ? pickPitch(pickScaleDegree(0, 6), 4) : baseLoosePitch;
        addNote(seq, 0, juce::jmax(1, ticksPerBeat / 2), p, 90, 1);
    }

    seq.addEvent(juce::MidiMessage::tempoMetaEvent(0.5), 0); // harmless default 120 BPM meta
    seq.updateMatchedPairs();
    return seq;
}

void BoomAudioProcessor::generateRollBatch(const juce::String& style, int tsNum, int tsDen,
    int bars, int howMany, const juce::File& folder,
    bool allowTriplets, bool allowDotted)
{
    if (!folder.exists())
        folder.createDirectory();

    const int safeHowMany = juce::jlimit(1, 200, howMany);

    for (int i = 0; i < safeHowMany; ++i)
    {
        // deterministic-ish but varied
        const int seed = (int)juce::Time::getMillisecondCounter() + (i * 99991);

        auto seq = generateRolls(style, tsNum, tsDen, bars, allowTriplets, allowDotted, seed);

        // Write standard MIDI file (format 1, single track is fine)
        juce::MidiFile mf;
        mf.setTicksPerQuarterNote(kPPQ);

        juce::MidiMessageSequence track(seq);
        track.updateMatchedPairs();
        mf.addTrack(track);

        const juce::String fileName = "BOOM_ROLL_" + style.trim().replaceCharacters(" /\\:", "____")
            + "_" + juce::String(tsNum) + "_" + juce::String(tsDen)
            + "_" + juce::String(bars) + "bars_"
            + juce::String(i + 1).paddedLeft('0', 3)
            + ".mid";

        juce::File out = folder.getChildFile(fileName);

        if (auto stream = out.createOutputStream())
        {
            mf.writeTo(*stream);
            stream->flush();
        }
    }
}


// ---------- REPLACE entire BoomAudioProcessor::generateDrums(int bars) with this ----------
void BoomAudioProcessor::generateDrums(int bars)
{
    // ============================================================
    // NEW DRUM ENGINE (fresh, style-first)
    // - No dependency on boom::drums generator or old style profiles
    // - Enforces per-style snare placement + per-style hat feel
    // - Uses APVTS params (bars/timeSig/restDensity/Triplets/Dotted/Swing)
    // ============================================================

    // clamp bars to a safe range
    bars = juce::jlimit(1, 8, bars);

    // --- read chosen drum style from APVTS (safe fallback to "trap") ---
    juce::String style = "trap";
    if (auto* prm = apvts.getParameter("drumStyle"))
        if (auto* ch = dynamic_cast<juce::AudioParameterChoice*>(prm))
            style = ch->getCurrentChoiceName();

    const juce::String styleLower = style.toLowerCase();

    // --- time signature helpers ---
    const int beatsPerBar = getTimeSigNumerator();
    const int denom = getTimeSigDenominator();
    constexpr int PPQ = 960;
    const int ticksPerBeat = PPQ * 4 / juce::jmax(1, denom);
    const int ticksPerBar = ticksPerBeat * juce::jmax(1, beatsPerBar);

    // --- density / feel params ---
    const int restPct = (int)juce::jlimit(0.0f, 100.0f,
        readParam(apvts, "restDensityDrums", readParam(apvts, "restDensity", 5.0f)));

    const bool useTriplets = readParam(apvts, "useTriplets", 0.0f) > 0.5f;
    const int tripletDensity = (int)juce::jlimit(0.0f, 100.0f, readParam(apvts, "tripletDensity", 0.0f));

    const bool useDotted = readParam(apvts, "useDotted", 0.0f) > 0.5f;
    const int dottedDensity = (int)juce::jlimit(0.0f, 100.0f, readParam(apvts, "dottedDensity", 0.0f));

    const int swingPct = (int)juce::jlimit(0.0f, 100.0f, readParam(apvts, "swing", 0.0f));

    // RNG (consistent with rest of BOOM: genNonce_ advances each click)
    juce::Random rng((int)genNonce_.fetch_add(1));

    // --- helpers ---
    auto chance = [&](int pct) -> bool
        {
            pct = juce::jlimit(0, 100, pct);
            return rng.nextInt(100) < pct;
        };

    auto applySwing = [&](int startTickInBar, int gridTicks) -> int
        {
            // Basic swing: delay every "off" step by up to ~1/16 note.
            // Only makes sense on even divisions (8ths/16ths). If gridTicks is 0, do nothing.
            if (swingPct <= 0 || gridTicks <= 0) return startTickInBar;

            // Determine which subdivision index this is (relative to bar)
            const int stepIndex = startTickInBar / gridTicks;
            const bool isOff = (stepIndex % 2) == 1;
            if (!isOff) return startTickInBar;

            const int maxDelay = juce::jmax(0, gridTicks / 2);          // don't delay past next grid
            const int delay = (maxDelay * swingPct) / 100;
            return startTickInBar + delay;
        };

    auto addNote = [&](juce::Array<Note>& out, int row, int bar, int startTickInBar, int lenTicks, int vel, int gridTicksForSwing)
        {
            Note n;
            n.row = row;
            n.startTick = bar * ticksPerBar + startTickInBar;
            n.lengthTicks = juce::jmax(1, lenTicks);
            n.velocity = juce::jlimit(1, 127, vel);

            // Only swing hats/open hats/percs (rows 2+), never swing kick/snare.
            if (row >= 2)
                n.startTick = bar * ticksPerBar + applySwing(startTickInBar, gridTicksForSwing);

            out.add(n);
        };

    const int kickLen = juce::jmax(12, ticksPerBeat / 4);
    const int snareLen = juce::jmax(12, ticksPerBeat / 4);
    const int hatLen = juce::jmax(6, ticksPerBeat / 6);
    const int openHatLen = juce::jmax(12, ticksPerBeat / 3);
    const int percLen = hatLen;

    // Rows used by DrumGridComponent:
    // 0 kick, 1 snare, 2 hihat, 3 openhat, 4 perc1, 5 perc2, 6 perc3
    static constexpr int ROW_KICK = 0;
    static constexpr int ROW_SNARE = 1;
    static constexpr int ROW_HAT = 2;
    static constexpr int ROW_OPENHAT = 3;
    static constexpr int ROW_PERC1 = 4;
    static constexpr int ROW_PERC2 = 5;
    static constexpr int ROW_PERC3 = 6;

    juce::Array<Note> out;
    out.ensureStorageAllocated(bars * 64);

    // ============================================================
    // 1) SNARE BACKBEAT (style-enforced)
    // ============================================================
    auto addSnareBackbeatForBar = [&](int bar)
        {
            // default: 2 & 4 (if possible)
            juce::Array<int> snareTicks;

            const bool isTrap = styleLower.contains("trap");
            const bool isDrill = styleLower.contains("drill");
            const bool isHipHop = styleLower.contains("hip") || styleLower.contains("boom bap") || styleLower.contains("boombap");
            const bool isWestie = styleLower.contains("west") || styleLower.contains("uk") || styleLower.contains("westie");
            const bool isReggaeton = styleLower.contains("reggaeton") || styleLower.contains("dembow");

            // Trap/drill in 4/4: clap on beat 3 of EVERY bar
            if ((isTrap || isDrill) && beatsPerBar == 4 && denom == 4)
            {
                // beat 3 = index 2 (0-indexed)
                snareTicks.add(2 * ticksPerBeat);
            }
            else if ((isTrap || isDrill) && beatsPerBar >= 3)
            {
                // non-4/4 trap/drill: clap on beat 3 (fallback)
                snareTicks.add(2 * ticksPerBeat);
            }
            else if (isReggaeton && beatsPerBar >= 4)
            {
                // reggaeton "dembow" feel (4/4):
                // snare/clap on the "and" of 2 (2.5) and on beat 4
                snareTicks.add(1 * ticksPerBeat + (ticksPerBeat / 2));
                snareTicks.add(3 * ticksPerBeat);
            }
            else if ((isHipHop || isWestie) && beatsPerBar >= 4)
            {
                // hip hop / westie: 2 & 4
                snareTicks.add(1 * ticksPerBeat);
                snareTicks.add(3 * ticksPerBeat);
            }
            else
            {
                // fallback: 2 & 4 when possible, otherwise "middle beat"
                if (beatsPerBar >= 4) { snareTicks.add(1 * ticksPerBeat); snareTicks.add(3 * ticksPerBeat); }
                else if (beatsPerBar >= 2) { snareTicks.add(1 * ticksPerBeat); }
                else { snareTicks.add(0); }
            }

            for (int i = 0; i < snareTicks.size(); ++i)
            {
                const int t = snareTicks[i];

                // backbeat is "protected": it should almost always exist.
                // restDensity can still remove it, but only at very high values.
                const int protectedRemovePct = juce::jlimit(0, 60, restPct / 2); // gentler than other lanes
                if (chance(protectedRemovePct)) continue;

                const int vel = 105 + rng.nextInt(18); // 105..122
                addNote(out, ROW_SNARE, bar, t, snareLen, vel, 0);
            }

            // Optional occasional ghost before a backbeat (hip hop / westie especially)
            if (beatsPerBar >= 2 && chance(18) && (styleLower.contains("hip") || styleLower.contains("west")))
            {
                // ghost on the "a" of 1 or "e" of 2 depending on random
                const int base = chance(50) ? (ticksPerBeat / 2) : (ticksPerBeat + ticksPerBeat / 4);
                const int vel = 55 + rng.nextInt(20);
                addNote(out, ROW_SNARE, bar, base, juce::jmax(6, snareLen / 2), vel, 0);
            }
        };

    // ============================================================
    // 2) KICK (style-guided, not fully locked)
    // ============================================================
    auto addKickForBar = [&](int bar)
        {
            // Always anchor beat 1 unless restPct is extreme
            if (!chance(juce::jlimit(0, 90, restPct)))
                addNote(out, ROW_KICK, bar, 0, kickLen, 110 + rng.nextInt(12), 0);

            const bool isTrap = styleLower.contains("trap");
            const bool isDrill = styleLower.contains("drill");
            const bool isReggaeton = styleLower.contains("reggaeton") || styleLower.contains("dembow");
            const bool isWestie = styleLower.contains("west");

            // Common grid options
            const int t8 = ticksPerBeat / 2;
            const int t16 = ticksPerBeat / 4;

            if (isReggaeton && beatsPerBar >= 4)
            {
                // Simple dembow-ish kick pattern: 1, (a of 1), 3, (and of 3)
                addNote(out, ROW_KICK, bar, 0, kickLen, 112 + rng.nextInt(10), 0);
                addNote(out, ROW_KICK, bar, t16 * 3, kickLen, 100 + rng.nextInt(15), 0);        // "a" of 1
                addNote(out, ROW_KICK, bar, 2 * ticksPerBeat, kickLen, 112 + rng.nextInt(10), 0); // beat 3
                if (chance(55)) addNote(out, ROW_KICK, bar, 2 * ticksPerBeat + t8, kickLen, 95 + rng.nextInt(20), 0);
                return;
            }

            // Trap/drill: sparse but punchy, with occasional syncopation
            if (isTrap || isDrill)
            {
                if (beatsPerBar >= 4 && chance(75)) addNote(out, ROW_KICK, bar, 2 * ticksPerBeat, kickLen, 108 + rng.nextInt(14), 0); // beat 3
                if (beatsPerBar >= 2 && chance(55)) addNote(out, ROW_KICK, bar, ticksPerBeat + t16 * (chance(50) ? 2 : 3), kickLen, 96 + rng.nextInt(20), 0);
                if (beatsPerBar >= 4 && chance(40)) addNote(out, ROW_KICK, bar, 3 * ticksPerBeat + t16 * (chance(50) ? 1 : 2), kickLen, 92 + rng.nextInt(22), 0);
                return;
            }

            // Westie: fairly minimal kick with bounce
            if (isWestie)
            {
                if (beatsPerBar >= 3 && chance(60)) addNote(out, ROW_KICK, bar, 2 * ticksPerBeat, kickLen, 104 + rng.nextInt(16), 0);
                if (beatsPerBar >= 4 && chance(35)) addNote(out, ROW_KICK, bar, 3 * ticksPerBeat + t8, kickLen, 90 + rng.nextInt(24), 0);
                return;
            }

            // Default (hip hop / everything else): kick on 1 and 3 with occasional extra
            if (beatsPerBar >= 3 && chance(70))
                addNote(out, ROW_KICK, bar, 2 * ticksPerBeat, kickLen, 104 + rng.nextInt(16), 0);

            // occasional extra somewhere on 16th grid (filtered by restPct)
            const int extraChance = juce::jlimit(5, 55, 35 - (restPct / 3));
            if (chance(extraChance))
            {
                const int steps16PerBar = juce::jmax(4, (ticksPerBar / t16));
                const int step16 = rng.nextInt(steps16PerBar);
                const int t = step16 * t16;

                // avoid clashing exactly with snare backbeats
                const bool nearSnare = (beatsPerBar >= 2 && (std::abs(t - ticksPerBeat) < t16))
                    || (beatsPerBar >= 4 && (std::abs(t - 3 * ticksPerBeat) < t16));
                if (!nearSnare)
                    addNote(out, ROW_KICK, bar, t, kickLen, 90 + rng.nextInt(25), 0);
            }
        };

    // ============================================================
    // 3) HI-HATS / OPEN HATS (style-first)
    // ============================================================
    auto addHatsForBar = [&](int bar)
        {
            const bool isTrap = styleLower.contains("trap");
            const bool isDrill = styleLower.contains("drill");
            const bool isHipHop = styleLower.contains("hip") || styleLower.contains("boom bap") || styleLower.contains("boombap");
            const bool isWestie = styleLower.contains("west");
            const bool isReggaeton = styleLower.contains("reggaeton") || styleLower.contains("dembow");

            // Decide the base grid for this bar (trap/hiphop: mostly 8ths; drill: triplets; westie: sparse)
            int gridTicks = ticksPerBeat / 2; // 8ths default
            if (isTrap && chance(15)) gridTicks = ticksPerBeat; // occasional quarters
            if (isReggaeton) gridTicks = ticksPerBeat / 2; // keep it simple & steady
            if (isDrill) gridTicks = ticksPerBeat / 3; // 8th triplets
            if (isWestie) gridTicks = ticksPerBeat / 2; // but we'll place sparsely

            // Build candidate hat positions within the bar
            juce::Array<int> hatTicks;
            if (isWestie)
            {
                // Westie: sparse (2..6 hits per bar on an 8th grid)
                const int slots = juce::jmax(1, ticksPerBar / gridTicks);
                const int wanted = juce::jlimit(2, 6, 2 + rng.nextInt(5));
                for (int k = 0; k < wanted; ++k)
                    hatTicks.add(rng.nextInt(slots) * gridTicks);

                hatTicks.sort();
                for (int i = hatTicks.size() - 1; i > 0; --i)
                {
                    if (hatTicks[i] == hatTicks[i - 1])
                        hatTicks.remove(i);
                }
            }
            else
            {
                // Regular: fill the grid
                for (int t = 0; t < ticksPerBar; t += gridTicks)
                    hatTicks.add(t);
            }

            // Apply rests to hats (this is where restDensity mostly matters)
            const int hatDropPct = juce::jlimit(0, 90, restPct + (isWestie ? 10 : 0));
            for (int i = hatTicks.size() - 1; i >= 0; --i)
                if (chance(hatDropPct))
                    hatTicks.remove(i);

            // If we removed too much, keep at least one hat per beat (except westie)
            if (!isWestie && hatTicks.size() < beatsPerBar)
            {
                hatTicks.clear();
                for (int beat = 0; beat < beatsPerBar; ++beat)
                    hatTicks.add(beat * ticksPerBeat);
            }

            // Add hats with human-ish velocities
            for (int i = 0; i < hatTicks.size(); ++i)
            {
                const int t = hatTicks[i];
                const int vel = 70 + rng.nextInt(25); // 70..94
                addNote(out, ROW_HAT, bar, t, hatLen, vel, gridTicks);
            }

            // Occasional open hat (usually on last offbeat)
            if (beatsPerBar >= 2 && chance(isReggaeton ? 35 : 22))
            {
                const int t = (beatsPerBar - 1) * ticksPerBeat + (ticksPerBeat / 2);
                if (t < ticksPerBar)
                    addNote(out, ROW_OPENHAT, bar, t, openHatLen, 85 + rng.nextInt(20), ticksPerBeat / 2);
            }

            // Rolls
            // - Trap: occasional 1-beat 16ths or burst
            // - Drill: frequent triplet rolls (16th triplets i.e. 6 per beat)
            // - Westie/HipHop: rare short burst
            const int baseRollChance =
                isDrill ? 45 :
                isTrap ? 22 :
                isWestie ? 18 :
                isHipHop ? 12 :
                10;

            if (chance(baseRollChance))
            {
                const int rollBeat = rng.nextInt(juce::jmax(1, beatsPerBar));
                int start = rollBeat * ticksPerBeat;

                // Roll resolution
                int rollGrid = ticksPerBeat / 4; // 16ths default
                int rollLenBeats = 1;

                if (isDrill)
                {
                    // 16th-triplet roll: 6 hits per beat -> grid = beat/6
                    rollGrid = ticksPerBeat / 6;
                    rollLenBeats = chance(30) ? 2 : 1;
                }
                else if (isTrap)
                {
                    // sometimes 32nds-ish feel
                    if (useTriplets && chance(25))
                        rollGrid = ticksPerBeat / 6; // triplet burst
                    else if (chance(30))
                        rollGrid = juce::jmax(1, (ticksPerBeat / 8)); // 32nd-ish
                    else
                        rollGrid = ticksPerBeat / 4; // 16ths
                    rollLenBeats = 1;
                }
                else
                {
                    rollGrid = ticksPerBeat / 4;
                    rollLenBeats = 1;
                }

                // Roll length in ticks
                const int rollLenTicks = rollLenBeats * ticksPerBeat;
                const int end = juce::jmin(ticksPerBar, start + rollLenTicks);

                // Optional dotted/triplet flavor (only if enabled and density suggests)
                if (useDotted && dottedDensity > 0 && chance(dottedDensity / 2) && rollGrid > 0)
                    rollGrid = (rollGrid * 3) / 2; // dotted = 1.5x grid
                if (useTriplets && tripletDensity > 0 && chance(tripletDensity / 2) && rollGrid > 0)
                    rollGrid = juce::jmax(1, rollGrid * 2 / 3); // triplet-ish = ~0.666x

                for (int t = start; t < end; t += juce::jmax(1, rollGrid))
                {
                    if (chance(juce::jlimit(0, 70, restPct))) continue;

                    const int vel = 60 + rng.nextInt(30);
                    addNote(out, ROW_HAT, bar, t, juce::jmax(3, hatLen / 2), vel, rollGrid);
                }
            }
        };

    // ============================================================
    // 4) PERC LANES (simple spice; safe defaults)
    // ============================================================
    auto addPercForBar = [&](int bar)
        {
            // Keep percs fairly light (they quickly make patterns feel "wrong")
            const int percChance = juce::jlimit(0, 40, 22 - (restPct / 4));
            if (!chance(percChance)) return;

            const int t16 = ticksPerBeat / 4;
            const int steps16 = juce::jmax(4, ticksPerBar / t16);

            const int hits = 1 + rng.nextInt(2); // 1..2 percs per bar
            for (int i = 0; i < hits; ++i)
            {
                const int step = rng.nextInt(steps16);
                const int t = step * t16;

                // pick a perc row
                const int r = rng.nextInt(3);
                const int row = (r == 0 ? ROW_PERC1 : (r == 1 ? ROW_PERC2 : ROW_PERC3));

                const int vel = 65 + rng.nextInt(30);
                addNote(out, row, bar, t, percLen, vel, t16);
            }
        };

    // ============================================================
    // Generate per bar
    // ============================================================
    for (int bar = 0; bar < bars; ++bar)
    {
        addSnareBackbeatForBar(bar);
        addKickForBar(bar);
        addHatsForBar(bar);
        addPercForBar(bar);
    }

    // ============================================================
    // Final cleanup: sort & dedupe identical lane/tick overlaps
    // ============================================================
    std::sort(out.begin(), out.end(), [](const Note& a, const Note& b)
        { return a.startTick < b.startTick; });

    // Remove exact duplicates (same row + same startTick)
    for (int i = out.size() - 1; i > 0; --i)
        if (out.getReference(i).row == out.getReference(i - 1).row
            && out.getReference(i).startTick == out.getReference(i - 1).startTick)
            out.remove(i);

    setDrumPattern(out); // setDrumPattern() already calls notifyPatternChanged()
    DBG("generateDrums (NEW): style=" << style << " bars=" << bars << " size=" << getDrumPattern().size());
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
// ===== makeHiHatPattern =====
// Fresh hats generator (no style profiles). Goals:
// - Time-signature aware (tsNum/tsDen), 4 or 8 bars typically (but any 1..8 works)
// - Style-aware rhythms (trap/drill/hiphop/wxstie + sensible defaults for others)
// - Optional tuned mode (APVTS "hatsTuned"): snaps pitches to APVTS key/scale
// - Avoid boring "root + 1" behavior: use wider degree selection / occasional anchor layer / roll types
juce::MidiMessageSequence BoomAudioProcessor::makeHiHatPattern(const juce::String& style,
    int tsNum, int tsDen, int bars,
    bool allowTriplets, bool allowDotted,
    int seed) const
{
    juce::ignoreUnused(allowDotted); // hats generator doesn't use dotted-note logic right now

    // -----------------------------
    // 0) Setup / constants
    // -----------------------------
    const int ppq = 96;               // BOOM global PPQ
    const int channel = 10;           // GM drums channel
    const int closedHat = 42;         // GM Closed HH
    const int openHat = 46;         // GM Open HH (rare accents)

    tsNum = juce::jlimit(1, 32, tsNum);
    tsDen = juce::jlimit(1, 32, tsDen);
    bars = juce::jlimit(1, 8, bars);

    // ticks for one beat-unit (the denominator note)
    const int ticksPerBeatUnit = juce::jmax(1, (ppq * 4) / tsDen);        // den=4 -> 96, den=8 -> 48 ...
    const int ticksPerBar = juce::jmax(1, ticksPerBeatUnit * tsNum);
    const int totalTicks = ticksPerBar * bars;

    // deterministic RNG when seed provided
    juce::Random rng(seed == -1 ? (int)juce::Time::getMillisecondCounter() : seed);

    const juce::String s = style.trim().toLowerCase();

    // tuned?
    const bool hatsTuned = readParam(apvts, "hatsTuned", 0.0f) > 0.5f;

    // read key / scale (only used when hatsTuned is on)
    juce::String keyName = "C";
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("key")))
        keyName = p->getCurrentChoiceName();

    juce::String scaleName = "Chromatic";
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("scale")))
        scaleName = p->getCurrentChoiceName();

    // scale pitch classes (kScales is already in this file)
    const auto itScale = kScales.find(scaleName.trim());
    const auto& scalePCs = (itScale != kScales.end()) ? itScale->second : kScales.at("Chromatic");
    const int scaleSize = (int)scalePCs.size();

    auto chooseBaseOctave = [&]() -> int
        {
            // mostly C3..C4 range (octave 3 or 4)
            return (rng.nextInt({ 100 }) < 70) ? 4 : 3;
        };

    auto chooseUntunedPitch = [&]() -> int
        {
            // mostly around C3..C4 (48..60), but not always exactly C
            return juce::jlimit(0, 127, 48 + rng.nextInt({ 13 })); // 48..60
        };

    auto chooseTunedPitch = [&](int degree, int octave) -> int
        {
            // degreeToPitch exists earlier in this file
            return degreeToPitch(keyName, scaleName, degree, octave);
        };

    // Choose a "musical" degree range that avoids always root/2nd.
    auto chooseWideDegree = [&]() -> int
        {
            if (scaleSize <= 0) return 0;

            const int r = rng.nextInt({ 100 });
            if (r < 18) return 0;                           // root sometimes
            if (r < 32) return 2 % scaleSize;               // 3rd-ish
            if (r < 50) return 4 % scaleSize;               // 5th-ish
            if (r < 70) return rng.nextInt({ scaleSize });  // any
            // spice: prefer upper-half degrees sometimes
            return juce::jlimit(0, scaleSize - 1,
                (scaleSize / 2) + rng.nextInt({ juce::jmax(1, scaleSize - scaleSize / 2) }));
        };

    enum RollType { Asc, Desc, Stationary };

    auto pickRollType = [&]() -> RollType
        {
            const int r = rng.nextInt({ 100 });
            if (r < 34) return Asc;
            if (r < 67) return Desc;
            return Stationary;
        };

    auto safeTicks = [&](int v) { return juce::jmax(1, v); };

    // Note lengths in ticks (derived from beat-unit)
    const int tQuarter = ticksPerBeatUnit;                 // denominator note
    const int tEighth = safeTicks(ticksPerBeatUnit / 2);
    const int tSixteenth = safeTicks(ticksPerBeatUnit / 4);
    const int tThirtySecond = safeTicks(ticksPerBeatUnit / 8);
    const int tSixtyFourth = safeTicks(ticksPerBeatUnit / 16);

    // Triplet lengths (relative to beat-unit)
    const int tEighthTriplet = safeTicks(ticksPerBeatUnit / 3);
    const int tSixteenthTriplet = safeTicks(ticksPerBeatUnit / 6);
    const int tThirtySecondTriplet = safeTicks(ticksPerBeatUnit / 12);

    // Quarter-triplet length (3 in the space of 2 quarters => 2/3 beat-unit)
    const int tQuarterTriplet = safeTicks((ticksPerBeatUnit * 2) / 3);

    // Choose base pitch + palette state
    const int baseOct = chooseBaseOctave();
    int baseDegree = chooseWideDegree();

    auto pickMainPitch = [&]() -> int
        {
            if (!hatsTuned) return chooseUntunedPitch();
            return chooseTunedPitch(baseDegree, baseOct);
        };

    auto pickPitchForRollStep = [&](RollType rt, int stepIndex, int basePitch) -> int
        {
            if (!hatsTuned)
            {
                int semis = 0;
                if (rt == Asc) semis = stepIndex;
                else if (rt == Desc) semis = -stepIndex;
                return juce::jlimit(0, 127, basePitch + semis);
            }

            int deg = baseDegree;
            if (rt == Asc) deg = baseDegree + stepIndex;
            else if (rt == Desc) deg = baseDegree - stepIndex;

            int oct = baseOct;
            if (scaleSize > 0)
            {
                const int wrap = (deg >= 0) ? (deg / scaleSize)
                    : -(((-deg) + scaleSize - 1) / scaleSize);
                oct = juce::jlimit(2, 6, baseOct + wrap);
            }
            return chooseTunedPitch(deg, oct);
        };

    // -----------------------------
    // 1) Decide generation path
    // -----------------------------
    const int pick24 = rng.nextInt({ 24 });
    const bool wantAnchor = (pick24 % 4) == 0;  // 1 in 4
    const bool wantGaps = (pick24 % 6) == 0;  // 1 in 6
    const bool wantRollHeavy = (pick24 % 8) == 0;  // 1 in 8

    int majorityTicks = tEighth; // default
    int baseVelMin = 70, baseVelMax = 95;
    int skipPct = 0;
    int openHatPct = 5;
    int rollChancePct = 12;

    if (s == "trap")
    {
        const int r = rng.nextInt({ 100 });
        if (r < 70) majorityTicks = tEighth;
        else        majorityTicks = tQuarter;

        if (allowTriplets && rng.nextInt({ 100 }) < 4)
            majorityTicks = tEighthTriplet;

        skipPct = wantGaps ? 12 : 2;
        rollChancePct = wantRollHeavy ? 35 : 18;
        openHatPct = 6;
    }
    else if (s == "drill")
    {
        const int r = rng.nextInt({ 100 });
        if (allowTriplets && r < 70) majorityTicks = tEighthTriplet;
        else if (allowTriplets && r < 95) majorityTicks = tQuarterTriplet;
        else majorityTicks = tEighth;

        skipPct = wantGaps ? 18 : 6;
        rollChancePct = wantRollHeavy ? 45 : 28;
        openHatPct = 8;
        baseVelMin = 68; baseVelMax = 98;
    }
    else if (s == "hip hop" || s == "hiphop")
    {
        const int r = rng.nextInt({ 100 });
        if (r < 78) majorityTicks = tEighth;
        else if (r < 97) majorityTicks = tQuarter;
        else majorityTicks = tSixteenth; // rare

        skipPct = wantGaps ? 18 : 4;
        rollChancePct = wantRollHeavy ? 30 : 14;
        openHatPct = 5;
        baseVelMin = 66; baseVelMax = 92;
    }
    else if (s == "wxstie" || s == "westie")
    {
        majorityTicks = (rng.nextInt({ 100 }) < 60) ? tEighth : tQuarter;

        // sparse most of the time
        skipPct = 55;
        rollChancePct = 18;
        openHatPct = 6;

        // 1 in 4: allow steadier pulse
        if (wantAnchor) { skipPct = 15; rollChancePct = 22; }
        // 1 in 8: riskier (still gappy, more rolls)
        if (wantRollHeavy) { skipPct = 40; rollChancePct = 35; }

        baseVelMin = 62; baseVelMax = 90;
    }
    else if (s == "rock" || s == "pop")
    {
        majorityTicks = (rng.nextInt({ 100 }) < 70) ? tEighth : tQuarter;
        skipPct = wantGaps ? 10 : 0;
        rollChancePct = wantRollHeavy ? 20 : 10;
        openHatPct = 4;
        baseVelMin = 75; baseVelMax = 105;
    }
    else if (s == "r&b" || s == "rnb")
    {
        majorityTicks = (rng.nextInt({ 100 }) < 70) ? tEighth : tQuarter;
        skipPct = wantGaps ? 18 : 6;
        rollChancePct = wantRollHeavy ? 28 : 14;
        openHatPct = 5;
        baseVelMin = 64; baseVelMax = 94;
    }
    else if (s == "reggaeton")
    {
        majorityTicks = (rng.nextInt({ 100 }) < 70) ? tEighth : tSixteenth;
        skipPct = wantGaps ? 18 : 8;
        rollChancePct = wantRollHeavy ? 26 : 12;
        openHatPct = 6;
        baseVelMin = 68; baseVelMax = 98;
    }
    else if (s == "edm")
    {
        const int r = rng.nextInt({ 100 });
        if (r < 50) majorityTicks = tEighth;
        else majorityTicks = tSixteenth;

        skipPct = wantGaps ? 10 : 2;
        rollChancePct = wantRollHeavy ? 40 : 18;
        openHatPct = 7;
        baseVelMin = 72; baseVelMax = 108;
    }
    else
    {
        majorityTicks = tEighth;
        skipPct = wantGaps ? 12 : 3;
        rollChancePct = wantRollHeavy ? 24 : 12;
        openHatPct = 5;
    }

    // -----------------------------
    // 2) Pattern building helpers
    // -----------------------------
    juce::MidiMessageSequence seq;

    auto randVel = [&]() -> int
        {
            int v = baseVelMin + rng.nextInt({ juce::jmax(1, baseVelMax - baseVelMin + 1) });
            if (rng.nextInt({ 100 }) < 12) v = juce::jmin(127, v + 12);
            return juce::jlimit(1, 127, v);
        };

    auto shouldSkip = [&]() -> bool
        {
            return rng.nextInt({ 100 }) < juce::jlimit(0, 100, skipPct);
        };

    auto pickOpenAccentPitch = [&](int basePitch) -> int
        {
            if (!hatsTuned || scalePCs.empty())
                return basePitch; // fallback (won't be used if you keep GM open-hat logic for untuned)

            // Prefer an accent above the base (5th-ish / octave-ish feel) but still in-scale.
            const int minUp = 5;   // at least a 4th/5th above
            const int maxUp = 12;  // up to an octave

            int best = basePitch;
            bool found = false;

            for (int n : scalePCs)
            {
                const int d = n - basePitch;
                if (d >= minUp && d <= maxUp)
                {
                    if (!found || d < (best - basePitch))
                    {
                        best = n;
                        found = true;
                    }
                }
            }

            // If nothing landed in that window, just pick the nearest higher scale tone we have.
            if (!found)
            {
                for (int n : scalePCs)
                {
                    if (n > basePitch) { best = n; found = true; break; }
                }
            }

            // Last resort: octave up clamped
            if (!found)
                best = juce::jlimit(0, 127, basePitch + 12);

            return juce::jlimit(0, 127, best);
        };


    auto addHat = [&](int tick, int len, int pitch, int vel)
        {
            addNote(seq,
                tick,
                juce::jmax(6, juce::jmin(len, ticksPerBeatUnit / 2)),
                pitch,
                vel,
                channel);
        };

    auto addRoll = [&](int startTick, int durTicks, RollType rt)
        {
            int unit = tThirtySecond;

            if (s == "trap")
            {
                const int r = rng.nextInt({ 100 });
                if (allowTriplets && r < 45) unit = tSixteenthTriplet;
                else if (allowTriplets && r < 70) unit = tThirtySecondTriplet;
                else unit = tThirtySecond;

                if (r > 96) unit = tSixtyFourth; // rare
            }
            else if (s == "drill")
            {
                const int r = rng.nextInt({ 100 });
                if (allowTriplets && r < 55) unit = tSixteenthTriplet;
                else if (allowTriplets && r < 85) unit = tThirtySecondTriplet;
                else unit = tThirtySecond;

                if (r > 96) unit = tSixtyFourth;
            }
            else
            {
                const int r = rng.nextInt({ 100 });
                if (allowTriplets && r < 35) unit = tSixteenthTriplet;
                else unit = tThirtySecond;

                if (r > 97) unit = tSixtyFourth;
            }

            unit = safeTicks(unit);

            const int basePitch = pickMainPitch();
            const int steps = juce::jlimit(2, 64, durTicks / unit);

            for (int i = 0; i < steps; ++i)
            {
                const int tick = startTick + i * unit;
                if (tick >= startTick + durTicks) break;

                const int pitch = pickPitchForRollStep(rt, i, basePitch);
                const int vel = juce::jlimit(1, 127, randVel() + (i == 0 ? 8 : 0));
                addHat(tick, unit, pitch, vel);
            }
        };

    auto addAnchorLayer = [&](int mainPitch)
        {
            int anchorPitch = mainPitch;

            if (!hatsTuned)
            {
                const int r = rng.nextInt({ 100 });
                const int semis = (r < 65) ? 7 : (r < 85) ? 12 : 5;
                anchorPitch = juce::jlimit(0, 127, mainPitch + semis);
            }
            else
            {
                int delta = 4;
                const int r = rng.nextInt({ 100 });
                if (r >= 65 && r < 85) delta = 3;
                else if (r >= 85) delta = 5;

                anchorPitch = chooseTunedPitch(baseDegree + delta, baseOct);
            }

            const int r = rng.nextInt({ 100 });
            int period = tQuarter;
            if (r < 45) period = tQuarter * 2;
            else if (r < 75) period = tQuarter;
            else period = juce::jmax(1, (tQuarter * 3) / 2);

            const int offset = (rng.nextInt({ 100 }) < 60) ? 0 : juce::jmax(0, period / 2);

            for (int t = offset; t < totalTicks; t += period)
            {
                if (rng.nextInt({ 100 }) < 18) continue;

                const int vel = juce::jlimit(1, 127, randVel() - 8);
                addHat(t, juce::jmax(6, period / 3), anchorPitch, vel);

                if (rng.nextInt({ 100 }) < 10)
                    addHat(t, juce::jmax(6, period / 3), openHat, juce::jmin(127, vel + 10));
            }
        };

    // -----------------------------
    // 3) Main pulse + rolls
    // -----------------------------
    const int mainPitch = pickMainPitch();

    if (s == "trap")
    {
        if (wantAnchor) addAnchorLayer(mainPitch);
    }
    else
    {
        if (wantAnchor && (rng.nextInt({ 100 }) < 45))
            addAnchorLayer(mainPitch);
    }

    for (int tick = 0; tick < totalTicks; tick += majorityTicks)
    {
        const bool isBarStart = (tick % ticksPerBar) == 0;

        if (!isBarStart && shouldSkip())
            continue;

        int vel = randVel();
        if (isBarStart) vel = juce::jmin(127, vel + 12);

        const bool doOpen = (rng.nextInt({ 100 }) < openHatPct) && (tick + majorityTicks < totalTicks);
        int pitch = 0;

        if (doOpen)
            pitch = hatsTuned ? pickOpenAccentPitch(mainPitch) : openHat;  // <-- key fix
        else
            pitch = hatsTuned ? mainPitch : closedHat;


        addHat(tick, majorityTicks, pitch, vel);

        // Trap: rare 16th burst when majority is eighth
        if (s == "trap" && majorityTicks == tEighth && rng.nextInt({ 100 }) < 3)
        {
            const int burstDur = (rng.nextInt({ 100 }) < 50) ? (ticksPerBar / 4) : (ticksPerBar / 2);
            const int endTick = juce::jmin(totalTicks, tick + burstDur);
            for (int t = tick; t < endTick; t += tSixteenth)
            {
                if (rng.nextInt({ 100 }) < 4) continue;
                addHat(t, tSixteenth, mainPitch, juce::jlimit(1, 127, randVel() - 10));
            }
        }

        // micro ghost
        if (rng.nextInt({ 100 }) < 10)
        {
            const int ghostTick = tick + juce::jmax(1, majorityTicks / 2);
            if (ghostTick < totalTicks)
                addHat(ghostTick, tThirtySecond, mainPitch, juce::jmax(12, vel - 28));
        }

        // roll insertion
        if (rng.nextInt({ 100 }) < rollChancePct)
        {
            const int rollStart = tick + juce::jmax(1, majorityTicks / 2);
            const int maxDur = juce::jmin(juce::jmax(1, ticksPerBar / 2), totalTicks - rollStart);

            const int durChoices[3] =
            {
                juce::jmax(1, ticksPerBeatUnit / 2),
                juce::jmax(1, ticksPerBeatUnit),
                juce::jmax(1, (ticksPerBeatUnit * 3) / 2)
            };

            int dur = durChoices[rng.nextInt({ 3 })];
            dur = juce::jmin(dur, maxDur);

            if (dur >= juce::jmax(1, tSixteenth))
                addRoll(rollStart, dur, pickRollType());
        }

        // hip-hop “majority note changes” (your 1-in-4 rule wants this sometimes)
        if ((s == "hip hop" || s == "hiphop") && wantGaps && rng.nextInt({ 100 }) < 6)
            baseDegree = chooseWideDegree();
    }

    // -----------------------------
    // 4) Risk overlays (gaps/sync + roll-heavy)
    // -----------------------------
    if (wantGaps || wantRollHeavy)
    {
        const int layerHits = wantRollHeavy ? (bars * 6) : (bars * 4);

        for (int i = 0; i < layerHits; ++i)
        {
            const int bar = rng.nextInt({ bars });
            const int barStart = bar * ticksPerBar;

            int offset = 0;
            const int r = rng.nextInt({ 100 });

            if ((s == "drill" || allowTriplets) && r < 45)
                offset = tEighthTriplet;
            else if (r < 70)
                offset = tEighth;
            else
                offset = juce::jmax(1, (tQuarter * 3) / 4);

            const int beat = rng.nextInt({ tsNum });
            int tick = barStart + beat * tQuarter + offset;
            if (tick < 0 || tick >= totalTicks) continue;
            if (tick + tThirtySecond >= totalTicks) continue;

            int pitch = mainPitch;
            if (hatsTuned)
            {
                if (rng.nextInt({ 100 }) < 40)
                    pitch = chooseTunedPitch(baseDegree + (rng.nextInt({ 5 }) - 2), baseOct);
            }
            else
            {
                if (rng.nextInt({ 100 }) < 35)
                    pitch = juce::jlimit(0, 127, mainPitch + (rng.nextInt({ 5 }) - 2));
            }

            addHat(tick, tThirtySecond, pitch, juce::jlimit(1, 127, randVel() - 12));

            if (wantRollHeavy && rng.nextInt({ 100 }) < 30)
            {
                const int dur = juce::jmax(1, ticksPerBeatUnit / 2);
                addRoll(tick, dur, pickRollType());
            }
        }
    }

    // fallback
    if (seq.getNumEvents() == 0)
    {
        for (int t = 0; t < totalTicks; t += tEighth)
            addHat(t, tEighth, mainPitch, 84);
    }

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
    juce::String ts = "4/4";
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("timeSig")))
        ts = p->getCurrentChoiceName();

    const auto parts = juce::StringArray::fromTokens(ts, "/", "");
    const int n = (parts.size() >= 1) ? parts[0].getIntValue() : 4;
    return juce::jlimit(1, 32, n);
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
    juce::String ts = "4/4";
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("timeSig")))
        ts = p->getCurrentChoiceName();

    const auto parts = juce::StringArray::fromTokens(ts, "/", "");
    const int d = (parts.size() >= 2) ? parts[1].getIntValue() : 4;
    return juce::jlimit(1, 32, d);
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