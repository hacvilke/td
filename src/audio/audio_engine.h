#pragma once
#include "mixer.h"
#include <cstdint>

namespace td {

class AudioEngine {
public:
    static const int NUM_BUFFERS = 4;
    static const int DEFAULT_BUFFER_SIZE = 4096;
    
    bool init(int sampleRate = 44100, int bufferSize = DEFAULT_BUFFER_SIZE);
    void shutdown();
    void update();
    
    // Sound playback
    int playSound(WAVData* wav, float volume = 1.0f, bool loop = false);
    void stopSound(int id);
    void stopAll();
    void pauseSound(int id);
    void resumeSound(int id);
    
    // Volume control
    void setMasterVolume(float volume);
    float getMasterVolume() const;
    void setSoundVolume(int id, float volume);
    void setSoundPan(int id, float pan);
    void setSoundPitch(int id, float pitch);
    
    bool isPlaying(int id) const;
    bool isInitialized() const { return m_initialized; }
    
    static AudioEngine& get();
    
private:
    AudioEngine() = default;
    ~AudioEngine();
    
    static void __stdcall waveOutCallback(void* hwo, uint32_t uMsg,
                                          uintptr_t dwInstance,
                                          uintptr_t dwParam1,
                                          uintptr_t dwParam2);
    
    bool openWaveDevice();
    void closeWaveDevice();
    void fillBuffer(int bufferIndex);
    
    void* m_waveOut = nullptr;
    void* m_headers[NUM_BUFFERS] = {};
    int16_t* m_buffers[NUM_BUFFERS] = {};
    
    int m_bufferSize = DEFAULT_BUFFER_SIZE;
    int m_sampleRate = 44100;
    int m_channels = 2;
    
    Mixer m_mixer;
    
    bool m_initialized = false;
    int m_currentBuffer = 0;
    
    static AudioEngine s_instance;
};

} // namespace td
