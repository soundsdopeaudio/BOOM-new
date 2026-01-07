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
        DBG("AudioFileRecorder::startRecording FAILED to open stream for file: " + fileToUse.getFullPathName());
        fileStream.reset();
        return false;
    }

    DBG("AudioFileRecorder::startRecording attempting writer: file=" + fileToUse.getFullPathName()
        + " sampleRate=" + juce::String(sampleRate) + " numChannels=" + juce::String(numChannels));

    juce::WavAudioFormat wavFormat;
    // 16-bit PCM
    writer.reset(wavFormat.createWriterFor(fileStream.get(), sampleRate, (unsigned int)numChannels, 16, {}, 0));

    if (!writer)
    {
        DBG("AudioFileRecorder::startRecording FAILED to create writer for file: " + fileToUse.getFullPathName());
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
        DBG("AudioFileRecorder::startRecording FAILED to create threadedWriter");
        backgroundThread.stopThread(100);
        return false;
    }

    DBG("AudioFileRecorder::startRecording OK -> recording started for: " + fileToUse.getFullPathName());
    recording.store(true);
    return true;
}

void AudioFileRecorder::stopRecording()
{
    const juce::ScopedLock sl(writerLock);

    if (!recording.load())
    {
        DBG("AudioFileRecorder::stopRecording called but not recording");
        return;
    }

    DBG("AudioFileRecorder::stopRecording: stopping");

    recording.store(false);

    // destroy threaded writer first to flush queued data
    threadedWriter.reset();

    // stop thread
    backgroundThread.stopThread(500);

    // writer already released to threadedWriter; nothing left to delete here
}

void AudioFileRecorder::onIncomingBuffer(const juce::AudioBuffer<float>& buffer, double /*sampleRate*/)
{
    // quick diagnostic so we can see whether buffers reach the recorder

    if (!recording.load()) return;

    const juce::ScopedLock sl(writerLock);
    if (!threadedWriter)
    {
        DBG("AudioFileRecorder::onIncomingBuffer EARLY-RETURN: no threadedWriter");
        return;
    }

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0) return;

    // ThreadedWriter expects an array of per-channel pointers: const float* const*
    std::vector<const float*> channelPtrs;
    channelPtrs.reserve((size_t)numChannels);
    for (int ch = 0; ch < numChannels; ++ch)
        channelPtrs.push_back(buffer.getReadPointer(ch));

    // write returns false on failure; log on failure
    bool ok = threadedWriter->write(channelPtrs.data(), numSamples);
    if (!ok)
        DBG("AudioFileRecorder::onIncomingBuffer write FAILED");
    else
        DBG("AudioFileRecorder::onIncomingBuffer write OK (" + juce::String(numSamples) + " frames)");
}