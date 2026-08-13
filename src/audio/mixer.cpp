#include "mixer.h"
#include "../core/math/math.h"
#include <cstring>
#include <cstdlib>
#include <malloc.h>

namespace td {

void Mixer::init(int sampleRate, int channels) {
    m_outputSampleRate = sampleRate;
    m_outputChannels = channels;
    m_nextId = 1;
    
    for (int i = 0; i < MAX_CHANNELS; i++) {
        m_channels[i] = MixerChannel();
    }
}

float Mixer::readSample(const WAVData* wav, uint32_t sampleIndex, int channel) const {
    if (!wav || !wav->data || sampleIndex >= wav->numSamples) {
        return 0.0f;
    }
    
    // Handle mono sources
    int srcChannel = (wav->numChannels == 1) ? 0 : channel;
    
    int bytesPerSample = wav->bitsPerSample / 8;
    uint32_t byteOffset = (sampleIndex * wav->numChannels + srcChannel) * bytesPerSample;
    
    if (byteOffset >= wav->dataSize) {
        return 0.0f;
    }
    
    // Convert to float [-1.0, 1.0]
    if (wav->bitsPerSample == 8) {
        // 8-bit is unsigned (0-255)
        uint8_t sample = wav->data[byteOffset];
        return ((float)sample - 128.0f) / 128.0f;
    }
    else if (wav->bitsPerSample == 16) {
        // 16-bit is signed
        int16_t sample = *(int16_t*)(wav->data + byteOffset);
        return (float)sample / 32768.0f;
    }
    else if (wav->bitsPerSample == 24) {
        // 24-bit is signed
        int32_t sample = (wav->data[byteOffset] |
                         (wav->data[byteOffset + 1] << 8) |
                         (wav->data[byteOffset + 2] << 16));
        // Sign extend
        if (sample & 0x800000) {
            sample |= 0xFF000000;
        }
        return (float)sample / 8388608.0f;
    }
    else if (wav->bitsPerSample == 32) {
        // 32-bit is signed
        int32_t sample = *(int32_t*)(wav->data + byteOffset);
        return (float)sample / 2147483648.0f;
    }
    
    return 0.0f;
}

void Mixer::mix(int16_t* output, int outputSamples) {
    // Clear output buffer
    memset(output, 0, outputSamples * m_outputChannels * sizeof(int16_t));
    
    // Accumulate in float for mixing
    float* mixBuffer = (float*)alloca(outputSamples * m_outputChannels * sizeof(float));
    memset(mixBuffer, 0, outputSamples * m_outputChannels * sizeof(float));
    
    for (int ch = 0; ch < MAX_CHANNELS; ch++) {
        MixerChannel& channel = m_channels[ch];
        
        if (!channel.playing || channel.paused || !channel.wav) {
            continue;
        }
        
        WAVData* wav = channel.wav;
        float volume = channel.volume * m_masterVolume;
        
        // Calculate pan volumes
        float leftVol = (channel.pan <= 0) ? 1.0f : (1.0f - channel.pan);
        float rightVol = (channel.pan >= 0) ? 1.0f : (1.0f + channel.pan);
        leftVol *= volume;
        rightVol *= volume;
        
        // Sample rate conversion ratio
        float pitchRatio = channel.pitch * ((float)wav->sampleRate / (float)m_outputSampleRate);
        
        for (int i = 0; i < outputSamples; i++) {
            // Get source sample (with interpolation for pitch shifting)
            float srcPos = channel.position + i * pitchRatio;
            uint32_t srcIndex = (uint32_t)srcPos;
            float frac = srcPos - (float)srcIndex;
            
            // Handle end of sound
            if (srcIndex >= wav->numSamples) {
                if (channel.looping) {
                    srcIndex %= wav->numSamples;
                } else {
                    channel.playing = false;
                    break;
                }
            }
            
            // Linear interpolation between samples
            uint32_t nextIndex = (srcIndex + 1) % wav->numSamples;
            
            for (int c = 0; c < m_outputChannels; c++) {
                float sample1 = readSample(wav, srcIndex, c);
                float sample2 = readSample(wav, nextIndex, c);
                float sample = sample1 + (sample2 - sample1) * frac;
                
                // Apply pan
                float panVol = (c == 0) ? leftVol : rightVol;
                
                mixBuffer[i * m_outputChannels + c] += sample * panVol;
            }
        }
        
        // Update channel position
        channel.position += (uint32_t)(outputSamples * pitchRatio);
        
        if (channel.position >= wav->numSamples) {
            if (channel.looping) {
                channel.position %= wav->numSamples;
            } else {
                channel.playing = false;
            }
        }
    }
    
    // Convert mix buffer to 16-bit output with clipping
    for (int i = 0; i < outputSamples * m_outputChannels; i++) {
        float sample = mixBuffer[i];
        
        // Soft clipping
        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;
        
        output[i] = (int16_t)(sample * 32767.0f);
    }
}

int Mixer::play(WAVData* wav, float volume, bool loop) {
    if (!wav) return -1;
    
    // Find free channel
    int freeChannel = -1;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (!m_channels[i].playing) {
            freeChannel = i;
            break;
        }
    }
    
    if (freeChannel == -1) {
        return -1; // No free channels
    }
    
    MixerChannel& channel = m_channels[freeChannel];
    channel.wav = wav;
    channel.position = 0;
    channel.volume = volume;
    channel.pan = 0.0f;
    channel.pitch = 1.0f;
    channel.playing = true;
    channel.looping = loop;
    channel.paused = false;
    channel.id = m_nextId++;
    
    return channel.id;
}

void Mixer::stop(int channelId) {
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (m_channels[i].id == channelId) {
            m_channels[i].playing = false;
            m_channels[i].id = -1;
            return;
        }
    }
}

void Mixer::stopAll() {
    for (int i = 0; i < MAX_CHANNELS; i++) {
        m_channels[i].playing = false;
        m_channels[i].id = -1;
    }
}

void Mixer::pause(int channelId) {
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (m_channels[i].id == channelId) {
            m_channels[i].paused = true;
            return;
        }
    }
}

void Mixer::resume(int channelId) {
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (m_channels[i].id == channelId) {
            m_channels[i].paused = false;
            return;
        }
    }
}

void Mixer::setChannelVolume(int channelId, float volume) {
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (m_channels[i].id == channelId) {
            m_channels[i].volume = clamp(volume, 0.0f, 2.0f);
            return;
        }
    }
}

void Mixer::setChannelPan(int channelId, float pan) {
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (m_channels[i].id == channelId) {
            m_channels[i].pan = clamp(pan, -1.0f, 1.0f);
            return;
        }
    }
}

void Mixer::setChannelPitch(int channelId, float pitch) {
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (m_channels[i].id == channelId) {
            m_channels[i].pitch = clamp(pitch, 0.1f, 4.0f);
            return;
        }
    }
}

bool Mixer::isPlaying(int channelId) const {
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (m_channels[i].id == channelId) {
            return m_channels[i].playing && !m_channels[i].paused;
        }
    }
    return false;
}

int Mixer::getActiveChannels() const {
    int count = 0;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (m_channels[i].playing) count++;
    }
    return count;
}

} // namespace td
