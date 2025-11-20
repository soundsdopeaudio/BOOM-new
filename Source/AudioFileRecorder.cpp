#include "AudioFileRecorder.h"
#include <vector>

AudioFileRecorder::AudioFileRecorder()
{
    // background thread is started only when we create ThreadedWriter (but safe to start lazy)
}

AudioFileRecorder::~AudioFileRecorder()
{
    stopRecording();
}

bool AudioFileRecorder::startRecording(const juce::File& fileToUse, double sampleRate, int numChannels)
{
    stopRecording();

    if (sampleRate <= 0.0 || numChannels <= 0)
        return false;

    // Ensure parent exists
    fileStream.reset(new juce::FileOutputStream(fileToUse));

    if (!fileStream || !fileStream->openedOk())
    {
        fileStream.reset();
        return false;
    }

    juce::WavAudioFormat wavFormat;
    // 16-bit PCM
    writer.reset(wavFormat.createWriterFor(fileStream.get(), sampleRate, (unsigned int)numChannels, 16, {}, 0));

    if (!writer)
    {
        fileStream.reset();
        return false;
    }

    // Transfer ownership of stream to writer (writer will delete the stream when deleted)
    fileStream.release();

    // Start background thread and create ThreadedWriter which buffers writes off the audio thread.
    // startThread uses the default priority when no argument is given
    backgroundThread.startThread();
    threadedWriter.reset(new juce::AudioFormatWriter::ThreadedWriter(writer.release(), backgroundThread, 32768));

    if (!threadedWriter)
    {
        backgroundThread.stopThread(100);
        return false;
    }

    recording.store(true);
    return true;
}

void AudioFileRecorder::stopRecording()
{
    const juce::ScopedLock sl(writerLock);

    if (!recording.load()) return;

    recording.store(false);

    // destroy threaded writer first to flush queued data
    threadedWriter.reset();

    // stop thread
    backgroundThread.stopThread(500);

    // writer already released to threadedWriter; nothing left to delete here
}

void AudioFileRecorder::onIncomingBuffer(const juce::AudioBuffer<float>& buffer, double /*sampleRate*/)
{
    if (!recording.load()) return;

    const juce::ScopedLock sl(writerLock);
    if (!threadedWriter) return;

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0) return;

    // ThreadedWriter expects an array of per-channel pointers: const float* const*
    // Build a small array of pointers pointing into the buffer's channels and call write().
    std::vector<const float*> channelPtrs;
    channelPtrs.reserve((size_t)numChannels);
    for (int ch = 0; ch < numChannels; ++ch)
        channelPtrs.push_back(buffer.getReadPointer(ch));

    // write returns false on failure; we ignore it here
    threadedWriter->write(channelPtrs.data(), numSamples);
}