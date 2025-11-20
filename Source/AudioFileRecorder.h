#pragma once

#include <JuceHeader.h>

// Simple helper that writes incoming float buffers to a WAV using a background ThreadedWriter.
// Implementation lives in AudioFileRecorder.cpp.
class AudioFileRecorder
{
public:
    AudioFileRecorder();
    ~AudioFileRecorder();

    // Start/stop recording to the given file. Returns true on success.
    bool startRecording(const juce::File& fileToUse, double sampleRate, int numChannels);

    // Stop any in-progress recording (flushes and stops background thread).
    void stopRecording();

    // Called from audio thread with fixed-size chunks delivered by AudioInputManager.
    void onIncomingBuffer(const juce::AudioBuffer<float>& buffer, double sampleRate);

    // Query recording state
    bool isRecording() const noexcept { return recording.load(); }

private:
    juce::CriticalSection writerLock; // protects threadedWriter access
    std::unique_ptr<juce::FileOutputStream> fileStream;
    std::unique_ptr<juce::AudioFormatWriter> writer;
    juce::TimeSliceThread backgroundThread{ "AudioFileRecorderThread" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;
    std::atomic<bool> recording{ false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioFileRecorder)
};