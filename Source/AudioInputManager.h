#pragma once

#include <JuceHeader.h>
#include "AudioFileRecorder.h"

/*
  AudioInputManager

  - Owns a juce::AudioDeviceManager.
  - Presents a juce::AudioDeviceSelectorComponent (you can add it to your UI).
  - Implements AudioIODeviceCallback to receive audio input callbacks.
  - Buffers audio into a lock-free FIFO (interleaved frames per channel).
  - Exposes a std::function<void(const juce::AudioBuffer<float>&, double sampleRate)> onBufferReady
    that will be called periodically from the audio thread (in the audio callback) when we have
    a full chunk available.
*/

class AudioInputManager : private juce::AudioIODeviceCallback
{
public:
    AudioInputManager();
    ~AudioInputManager();

    // Call this to attach selector component to your UI
    juce::AudioDeviceSelectorComponent* getDeviceSelectorComponent() noexcept { return selectorComponent.get(); }

    // Start / Stop receiving audio callbacks (does not write to disk)
    void start();
    void stop();
    bool isRunning() const noexcept { return running.load(); }

    // Set how many frames per buffer we deliver to the consumer callback (power of two preferred)
    void setCallbackBlockSize(int blockSize) noexcept { callbackBlockSize.store(blockSize); }

    // Called on audio thread when a block is ready. Provide a function to receive the float buffer.
    // The buffer passed to onBufferReady is owned by the callee for the duration of the call only.
    std::function<void(const juce::AudioBuffer<float>& buffer, double sampleRate)> onBufferReady;

    // Peak meter reading (thread-safe approximate)
    float getInputRMS() const noexcept { return peakRms.load(); }

    // --- Recorder integration
    // Start recording to a WAV file. Returns true on success.
    bool startRecordingToFile(const juce::File& fileToUse);
    // Stop an in-progress recording (if any).
    void stopRecordingToFile();
    // Query active recorder state.
    bool isRecordingToFile() const noexcept;

    // Try to open the named input device. Returns true on success.
    bool openInputDeviceByName(const juce::String& inputDeviceName);

    // Returns the currently-selected input device name (from the AudioDeviceManager setup)
    juce::String getCurrentInputDeviceName() const noexcept;

private:
    // AudioIODeviceCallback
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

       // Legacy-style callback (kept for the implementation). Not marked override because JUCE
    // exposes the "with context" version in this JUCE version.
    void audioDeviceIOCallback(const float* const* inputChannelData, int numInputChannels,
                               float* const* outputChannelData, int numOutputChannels,
                               int numSamples);

    // Newer JUCE device types call the "with context" overload. Implement it and forward to the
    // legacy callback so the rest of the logic remains unchanged.
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                          float* const* outputChannelData, int numOutputChannels,
                                          int numSamples, const juce::AudioIODeviceCallbackContext& context) override;

    // helper
    void pushToFifo(const juce::AudioBuffer<float>& src);

    juce::AudioDeviceManager deviceManager;

    std::unique_ptr<juce::AudioDeviceSelectorComponent> selectorComponent;

    // Ring buffer: store interleaved frames per channel for callbackBlockSize delivery
    // We'll use a simple circular buffer of channels x frames
    juce::AbstractFifo fifo{ 480 * 100 }; // reserve capacity in frames (adjust if you want longer) - default 100 seconds at 48k
    juce::AudioBuffer<float> ringBuffer;   // sized to channels x capacity
    int ringWritePos = 0;
    int ringCapacity = 0;

    // Temp buffer for delivering fixed-size chunks to the consumer
    juce::AudioBuffer<float> deliverBuffer;

    std::atomic<int> callbackBlockSize{ 512 }; // default chunk size
    std::atomic<bool> running{ false };

    std::atomic<float> peakRms{ 0.0f };

    // Recorder instance wired into this manager (writes using a background thread)
    AudioFileRecorder recorder;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioInputManager)
};