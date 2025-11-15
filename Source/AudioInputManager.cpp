#include "AudioInputManager.h"

AudioInputManager::AudioInputManager()
{
    // initialise device manager with default input channel
    // We'll request 0 output channels (host might force outputs), 2 input channels (stereo)
    // true = wantInput, true = wantOutput (we set true so selector can show both), default device types, nullptr callback
    const int numInitialInputChannels = 2;
    const int numInitialOutputChannels = 0;

    deviceManager.initialise(numInitialInputChannels, numInitialOutputChannels, nullptr, true);

    // create selector component (show input & output devices, sample rate, buffer size)
    selectorComponent.reset(new juce::AudioDeviceSelectorComponent(deviceManager,
        0,   // min input channels shown
        16,  // max input channels shown
        0,   // min output channels shown
        16,  // max output channels shown
        true, // showMidiInputOptions
        true, // showMidiOutputOptions
        true, // showChannelsAsStereoPairs
        true  // hideAdvancedOptions
    ));

    // initial ring buffer capacity based on sample rate guess; will be resized in audioDeviceAboutToStart
    ringCapacity = 480 * 100; // frames
    ringBuffer.setSize(2, ringCapacity); // 2 channels by default
    // use .load() to read atomic
    deliverBuffer.setSize(2, static_cast<int>(callbackBlockSize.load()));
}

AudioInputManager::~AudioInputManager()
{
    stop();
    deviceManager.removeAudioCallback(this);
    selectorComponent.reset();
    deviceManager.closeAudioDevice();
}

void AudioInputManager::start()
{
    if (running.load()) return;

    // ensure we are registered for audio callbacks
    deviceManager.addAudioCallback(this);
    running = true;
}

void AudioInputManager::stop()
{
    if (!running.load()) return;

    running = false;
    deviceManager.removeAudioCallback(this);
}

void AudioInputManager::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    const double sr = device ? device->getCurrentSampleRate() : 44100.0;
    const int numCh = device ? device->getActiveInputChannels().countNumberOfSetBits() : 2;

    // resize ring buffer
    const int desiredCapacityFrames = static_cast<int> (sr * 60.0); // keep ~60s max by default (adjustable)
    ringCapacity = juce::jmax(1024, desiredCapacityFrames);
    ringBuffer.setSize(juce::jmax(1, numCh), ringCapacity);
    ringWritePos = 0;

    // ensure deliverBuffer matches channels and callbackBlockSize
    const int cb = callbackBlockSize.load();
    deliverBuffer.setSize(juce::jmax(1, numCh), cb);

    // reset peak
    peakRms = 0.0f;
}

void AudioInputManager::audioDeviceStopped()
{
    // nothing special
}

// Match signature declared in header (must exactly match JUCE's AudioIODeviceCallback for override to work)
void AudioInputManager::audioDeviceIOCallback(const float** inputChannelData, int numInputChannels,
    float** /*outputChannelData*/, int /*numOutputChannels*/,
    int numSamples)
{
    if (!running.load())
        return;

    if (numInputChannels <= 0 || inputChannelData == nullptr)
        return;

    const int channels = juce::jmin(ringBuffer.getNumChannels(), numInputChannels);

    // compute RMS for simple peak meter
    float sumSq = 0.0f;
    int totalSamples = 0;
    for (int ch = 0; ch < channels; ++ch)
    {
        const float* in = inputChannelData[ch];
        if (in == nullptr) continue;
        for (int i = 0; i < numSamples; ++i)
        {
            const float v = in[i];
            sumSq += v * v;
            ++totalSamples;
        }
    }

    if (totalSamples > 0)
    {
        const float rms = std::sqrt(sumSq / (float)totalSamples);
        // smooth a bit (simple one-pole)
        float prev = peakRms.load();
        float newVal = prev * 0.85f + rms * 0.15f;
        peakRms = newVal;
    }

    // copy into ring buffer (per-channel)
    const int framesToWrite = numSamples;
    for (int ch = 0; ch < channels; ++ch)
    {
        const float* src = inputChannelData[ch];
        float* dst = ringBuffer.getWritePointer(ch);
        int writePos = ringWritePos;

        // may wrap
        if (writePos + framesToWrite <= ringCapacity)
        {
            memcpy(dst + writePos, src, sizeof(float) * (size_t)framesToWrite);
        }
        else
        {
            const int firstPart = ringCapacity - writePos;
            memcpy(dst + writePos, src, sizeof(float) * (size_t)firstPart);
            memcpy(dst, src + firstPart, sizeof(float) * (size_t)(framesToWrite - firstPart));
        }
    }

    // advance write pos
    this->ringWritePos = (this->ringWritePos + framesToWrite) % this->ringCapacity;

    // Try to deliver fixed-size blocks to onBufferReady
    // We'll determine how many whole blocks we have in ringBuffer: this is a simple approach:
    // copy the latest 'callbackBlockSize' frames into deliverBuffer and call onBufferReady.
    // This gives overlapping behavior (we'll deliver contiguous latest blocks).
    const int cb = callbackBlockSize.load();
    if (cb <= 0)
        return;

    // build deliverBuffer from the newest cb frames
    const int startPos = (this->ringWritePos - cb + ringCapacity) % ringCapacity;
    const int channelsToDeliver = juce::jmin(ringBuffer.getNumChannels(), numInputChannels);
    for (int ch = 0; ch < channelsToDeliver; ++ch)
    {
        float* dest = this->deliverBuffer.getWritePointer(ch);
        const float* src = this->ringBuffer.getReadPointer(ch);

        if (startPos + cb <= this->ringCapacity)
        {
            memcpy(dest, src + startPos, sizeof(float) * (size_t)cb);
        }
        else
        {
            const int firstPart = this->ringCapacity - startPos;
            memcpy(dest, src + startPos, sizeof(float) * (size_t)firstPart);
            memcpy(dest + firstPart, src, sizeof(float) * (size_t)(cb - firstPart));
        }
    }

    // call consumer on audio thread (be careful: expensive work must not block audio thread)
    if (static_cast<bool>(onBufferReady))
    {
        double sampleRate = 44100.0;
        if (auto* dev = deviceManager.getCurrentAudioDevice())
            sampleRate = dev->getCurrentSampleRate();
        this->onBufferReady(deliverBuffer, sampleRate);
    }
}

void AudioInputManager::pushToFifo(const juce::AudioBuffer<float>& /*src*/)
{
    // not used in this implementation (we push directly from callback)
}
