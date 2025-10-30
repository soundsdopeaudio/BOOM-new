#pragma once
#include <JuceHeader.h>
#include "EngineDefs.h"
#include <atomic>   // (at top of file if not already there)
#include <cstdint>
#include <functional>



class BoomAudioProcessor : public juce::AudioProcessor
{
public:
    BoomAudioProcessor();
    ~BoomAudioProcessor() override = default;

    std::atomic<std::uint64_t> genNonce_{ 1 };

    // ==== GEN: high-level generation entry points (engines) ====
// 808 generator entry point (called from UI)

    void generate808(int bars,
        int keyIndex,
        const juce::String& scaleName,
        int octave,
        int restPct,
        int dottedPct,
        int tripletPct,
        int swingPct,
        int seed = -1);
    void generateBass(int bars);
    void generateDrums(int bars);

    void generateBassFromSpec(const juce::String& styleName,
        int bars,
        int octave,
        int restPct,
        int dottedPct,
        int tripletPct,
        int swingPct,
        int seed = -1);

    double getHostBpm() const noexcept;

    // ==== GEN: edit/variation tools ====
    void bumpDrumRowsUp();                                         // for Bumppit (Drums)
    void transposeMelodic(int semitones, const juce::String& newKey, const juce::String& newScale, int octaveOffset); // for Bumppit (808/Bass)

    // ==== GEN: Rolls (Drums only) ====
    void generateDrumRolls(const juce::String& style, int bars);

    // Call when patterns change so the UI refreshes
    void notifyPatternChanged();

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

    // --- AI capture transport (playback + seek) ---
    void   aiPreviewStart();
    void   aiPreviewStop();
    bool   aiIsPreviewing() const noexcept { return isPreviewing.load(); }
    double getCaptureLengthSeconds() const noexcept;
    double getCapturePositionSeconds() const noexcept; // current preview read head in seconds
    void   aiSeekToSeconds(double sec) noexcept;

    // AudioProcessor
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override; // keep if already present
    bool BoomAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
    {
        // we’ll accept stereo or mono inputs/outputs
        const auto& mainIn = layouts.getMainInputChannelSet();
        const auto& mainOut = layouts.getMainOutputChannelSet();

        if (mainIn == juce::AudioChannelSet::mono() || mainIn == juce::AudioChannelSet::stereo())
            if (mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo())
                return true;

        return false; // always return something
    }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // DRUMS-ONLY Rolls generator for the Rolls window
    void generateRolls(const juce::String& styleName, int bars);
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    const juce::String getName() const override { return "BOOM"; }

    // MIDI effect flags
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Programs
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    int    getCaptureLengthSamples() const { return captureLengthSamples; }
    double getCaptureSampleRate()   const { return lastSampleRate; }
    float  getInputRMSL() const noexcept { return rmsInputL.load(); }
    float  getInputRMSR() const noexcept { return rmsInputR.load(); }
    int    getCapturePlayheadSamples() const noexcept { return capturePlayheadSamples.load(); }

    // State
    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Pattern model used by the Editor
    struct Note
    {
        int pitch{ 60 };   // used for 808/Bass
        int row{ 0 };      // used for Drums
        int startTick{ 0 };
        int lengthTicks{ 24 };
        int velocity{ 100 };
        int channel{ 1 };
    };
    using Pattern = juce::Array<Note>;


    const Pattern& getDrumPattern() const noexcept { return drumPattern; }
    const Pattern& getMelodicPattern() const noexcept { return melodicPattern; }
    void setDrumPattern(const Pattern& p) { drumPattern = p; }
    void setMelodicPattern(const Pattern& p) { melodicPattern = p; }
    void generateMelodicImpl(const juce::String& style,
        const juce::String& keyName,
        const juce::String& scaleName,
        int bars,
        int densityPercent,
        bool allowTriplets,
        bool allowDotted,
        int octave);

    const juce::StringArray& getDrumRows() const { return drumRows; }

    // PluginProcessor.cpp
    boom::Engine BoomAudioProcessor::getEngineSafe() const
    {
        // If the param is missing/null, default to Drums so we never crash.
        if (auto* p = apvts.getRawParameterValue("engine"))
            return static_cast<boom::Engine>(static_cast<int>(p->load()));

        return boom::Engine::Drums;
    }

    // Simple transforms
// ---- Bumppit (Drums path + Melodic path) ----                                               // DRUMS ONLY
    void bumppitTranspose(int targetKeyIndex, const juce::String& scaleName,  // 808/BASS ONLY
        int octaveDelta /* e.g., -2..+2 */);
    void flipMelodic(int densityPct, int addPct, int removePct);
    void flipDrums(int densityPct, int addPct, int removePct);

    // APVTS

    // === AI tools & generation wiring ===
    void aiStyleBlendDrums(const juce::String& styleA, const juce::String& styleB, int bars, float wA, float wB);
    void aiSlapsmithExpand(int bars);

    // randomizes the currently-selected engine’s parameters (key/scale/bars/etc) & generates
    void randomizeCurrentEngine(int bars);

    // rolls (drums only) – inject stylistic rolls/fills for 'bars' bars. seed = -1 uses random device
    void generateRolls(const juce::String& style, int bars, int seed);


    // === AI: Audio Capture Sources ===
    enum class CaptureSource { Loopback, Microphone };

    // Start/stop capture; in plugin context both read from processBlock input.
    // Allocate/resize the mono capture buffer for a given number of seconds
    void ensureCaptureCapacitySeconds(double seconds);
    void aiStartCapture(CaptureSource src);
    void aiStopCapture(CaptureSource src);
    bool aiIsCapturing() const { return ai_rh_rec_.load() || ai_bx_rec_.load(); }
    bool aiHasCapture() const noexcept { return captureLengthSamples > 0; }

    void ai_beginRhRecord();
    void ai_endRhRecord();
    bool ai_isRhRecording() const noexcept;

    void ai_beginBxRecord();
    void ai_endBxRecord();
    bool ai_isBxRecording() const noexcept;

    void ai_stopAllAIPlayback(); // stub for now (if you later add local playback)
    void ai_tickAIMeters();      // called from editor timer to update meter RMS quickly

    // meter getters (thread-safe reads done by atomics)
    float ai_getRhInL() const noexcept { return ai_rh_inL_.load(); }
    float ai_getRhInR() const noexcept { return ai_rh_inR_.load(); }
    float ai_getBxInL() const noexcept { return ai_bx_inL_.load(); }
    float ai_getBxInR() const noexcept { return ai_bx_inR_.load(); }

    // Transcribe captured audio into a drum pattern (kick/snare/hat) for given bars/bpm
    void aiAnalyzeCapturedToDrums(int bars, int bpm);

    // 808 generator
    void generate808(const juce::String& style, const juce::String& keyName,
        const juce::String& scaleName, int bars,
        int densityPercent, bool allowTriplets, bool allowDotted);
    // --- Time signature + bars helpers ---
    int getTimeSigNumerator() const noexcept;
    
    int getTimeSigDenominator() const noexcept;
    
    int getBars() const;
    

    int q16(int bars)  const { return bars * 16; }    // 16th-note grid per bar (simple mental model)
    static constexpr int PPQ = 96;

    using APVTS = juce::AudioProcessorValueTreeState;
    APVTS apvts;

private:
    Pattern drumPattern, melodicPattern;
    juce::StringArray drumRows { boom::defaultDrumRows() };

    std::atomic<double> lastHostBpm { 120.0 };

    std::atomic<float> rmsInputL { 0.0f }, rmsInputR{ 0.0f };
    std::atomic<int>   capturePlayheadSamples { 0 }; // advanced when recording

    // Recording flags (atomic so UI can poll safely)
    std::atomic<bool> ai_rh_rec_{ false };
    std::atomic<bool> ai_bx_rec_{ false };

    // Simple ring buffers for capture (mono mix)
    juce::AudioBuffer<float> ai_rh_buf_;
    juce::AudioBuffer<float> ai_bx_buf_;
    int ai_rh_write_ = 0;
    int ai_bx_write_ = 0;

    // Levels (atomics written from processBlock, read by UI)
    std::atomic<float> ai_rh_inL_{ 0.0f }, ai_rh_inR_{ 0.0f };
    std::atomic<float> ai_bx_inL_{ 0.0f }, ai_bx_inR_{ 0.0f };


    // ---- Random helpers (use juce::Random to avoid std::mt19937 headaches) ----
    juce::Random prng;
    int irand(int lo, int hi) { return prng.nextInt(juce::Range<int>(lo, hi + 1)); }
    bool chance(int pct) { return prng.nextInt({ 0,100 }) < juce::jlimit(0, 100, pct); }

    // ---- Quantize helpers ----
     // 96 ticks/quarter (we used this elsewhere)
    int toTick16(int n)const { return n * (PPQ / 4); }  // 16th -> ticks

    // ---- Small melody utility for 808/Bass ----
    int pickScaleDegree(const juce::String& scaleName, int degree0to6);

    // ---- Internal: notify UI ----
    void sendUIChange();


    // === AI: capture ring buffer ===
    CaptureSource currentCapture{ CaptureSource::Loopback };

    // --- AI Tools internals ---
    juce::AudioBuffer<float> captureBuffer;   // mono scratch buffer
    int captureWritePos = 0;
    int captureLengthSamples = 0;             // how many valid samples recorded
    double lastSampleRate = 44100.0;

    void appendCaptureFrom(const juce::AudioBuffer<float>& in);

    // --- Playback state for previewing the captured audio ---
    std::atomic<bool> isPreviewing { false };
    int previewReadPos = 0;  // in samples, 0..captureLengthSamples

    // Analysis helpers
    Pattern transcribeAudioToDrums(const float* mono, int numSamples, int bars, int bpm) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BoomAudioProcessor)
};
