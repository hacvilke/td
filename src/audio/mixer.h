#pragma once
#include "wav_loader.h"
#include <cstdint>

namespace td {

struct MixerChannel {
    WAVData* wav = nullptr;
    uint32_t position = 0;      // Current sample position
    float volume = 1.0f;
    float pan = 0.0f;           // -1 = left, 0 = center, 1 = right
    float pitch = 1.0f;         // Playback speed multiplier
    bool playing = false;
    bool looping = false;
    bool paused = false;
    int id = -1;
};

class Mixer {
public:
    static const int MAX_CHANNELS = 32;
    
    void init(int sampleRate = 44100, int channels = 2);
    
    // Mix all playing channels into the output buffer
    void mix(int16_t* output, int outputSamples);
    
    // Channel management
    int play(WAVData* wav, float volume = 1.0f, bool loop = false);
    void stop(int channelId);
    void stopAll();
    void pause(int channelId);
    void resume(int channelId);
    void setChannelVolume(int channelId, float volume);
    void setChannelPan(int channelId, float pan);
    void setChannelPitch(int channelId, float pitch);
    bool isPlaying(int channelId) const;
    
    // Master volume
    void setMasterVolume(float vol) { m_masterVolume = vol; }
    float getMasterVolume() const { return m_masterVolume; }
    
    int getActiveChannels() const;
    
private:
    float readSample(const WAVData* wav, uint32_t sampleIndex, int channel) const;
    
    MixerChannel m_channels[MAX_CHANNELS];
    float m_masterVolume = 1.0f;
    int m_outputSampleRate = 44100;
    int m_outputChannels = 2;
    int m_nextId = 1;
};

} // namespace td
