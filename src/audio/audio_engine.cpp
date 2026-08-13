#include "audio_engine.h"
#include "../core/logger.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

namespace td {

AudioEngine AudioEngine::s_instance;

AudioEngine::~AudioEngine() {
    shutdown();
}

AudioEngine& AudioEngine::get() {
    return s_instance;
}

bool AudioEngine::init(int sampleRate, int bufferSize) {
    if (m_initialized) {
        return true;
    }
    
    m_sampleRate = sampleRate;
    m_bufferSize = bufferSize;
    m_channels = 2;
    
    m_mixer.init(sampleRate, m_channels);
    
    if (!openWaveDevice()) {
        TD_LOG_ERROR("Failed to open wave device");
        return false;
    }
    
    // Allocate buffers
    for (int i = 0; i < NUM_BUFFERS; i++) {
        m_buffers[i] = (int16_t*)malloc(m_bufferSize * m_channels * sizeof(int16_t));
        
        if (!m_buffers[i]) {
            TD_LOG_ERROR("Failed to allocate audio buffer %d", i);
            shutdown();
            return false;
        }
        
        memset(m_buffers[i], 0, m_bufferSize * m_channels * sizeof(int16_t));
        
        // Prepare wave header
        WAVEHDR* header = (WAVEHDR*)malloc(sizeof(WAVEHDR));
        memset(header, 0, sizeof(WAVEHDR));
        
        header->lpData = (LPSTR)m_buffers[i];
        header->dwBufferLength = m_bufferSize * m_channels * sizeof(int16_t);
        header->dwFlags = 0;
        
        MMRESULT result = waveOutPrepareHeader((HWAVEOUT)m_waveOut, header, sizeof(WAVEHDR));
        if (result != MMSYSERR_NOERROR) {
            TD_LOG_ERROR("Failed to prepare wave header %d: %u", i, result);
            free(header);
            shutdown();
            return false;
        }
        
        m_headers[i] = header;
        
        // Fill and queue the buffer
        fillBuffer(i);
        waveOutWrite((HWAVEOUT)m_waveOut, (LPWAVEHDR)m_headers[i], sizeof(WAVEHDR));
    }
    
    m_initialized = true;
    TD_LOG_INFO("Audio engine initialized (%d Hz, %d channels, %d buffer size)",
                m_sampleRate, m_channels, m_bufferSize);
    
    return true;
}

bool AudioEngine::openWaveDevice() {
    WAVEFORMATEX wfx;
    memset(&wfx, 0, sizeof(wfx));
    
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = (WORD)m_channels;
    wfx.nSamplesPerSec = (DWORD)m_sampleRate;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize = 0;
    
    MMRESULT result = waveOutOpen(
        (LPHWAVEOUT)&m_waveOut,
        WAVE_MAPPER,
        &wfx,
        (DWORD_PTR)waveOutCallback,
        (DWORD_PTR)this,
        CALLBACK_FUNCTION
    );
    
    if (result != MMSYSERR_NOERROR) {
        TD_LOG_ERROR("waveOutOpen failed: %u", result);
        return false;
    }
    
    return true;
}

void AudioEngine::closeWaveDevice() {
    if (m_waveOut) {
        waveOutReset((HWAVEOUT)m_waveOut);
        
        for (int i = 0; i < NUM_BUFFERS; i++) {
            if (m_headers[i]) {
                waveOutUnprepareHeader((HWAVEOUT)m_waveOut, 
                                       (LPWAVEHDR)m_headers[i], 
                                       sizeof(WAVEHDR));
                free(m_headers[i]);
                m_headers[i] = nullptr;
            }
        }
        
        waveOutClose((HWAVEOUT)m_waveOut);
        m_waveOut = nullptr;
    }
}

void AudioEngine::shutdown() {
    if (!m_initialized) {
        return;
    }
    
    m_mixer.stopAll();
    closeWaveDevice();
    
    for (int i = 0; i < NUM_BUFFERS; i++) {
        if (m_buffers[i]) {
            free(m_buffers[i]);
            m_buffers[i] = nullptr;
        }
    }
    
    m_initialized = false;
    TD_LOG_INFO("Audio engine shutdown");
}

void AudioEngine::fillBuffer(int bufferIndex) {
    if (bufferIndex < 0 || bufferIndex >= NUM_BUFFERS) return;
    
    // Mix audio into the buffer
    m_mixer.mix(m_buffers[bufferIndex], m_bufferSize);
}

void __stdcall AudioEngine::waveOutCallback(void* hwo, uint32_t uMsg,
                                             uintptr_t dwInstance,
                                             uintptr_t dwParam1,
                                             uintptr_t dwParam2) {
    (void)hwo;
    (void)dwParam2;
    
    if (uMsg != WOM_DONE) {
        return;
    }
    
    AudioEngine* engine = (AudioEngine*)dwInstance;
    WAVEHDR* header = (WAVEHDR*)dwParam1;
    
    // Find which buffer was completed
    int bufferIndex = -1;
    for (int i = 0; i < NUM_BUFFERS; i++) {
        if (engine->m_headers[i] == header) {
            bufferIndex = i;
            break;
        }
    }
    
    if (bufferIndex == -1) {
        return;
    }
    
    // Refill and requeue
    engine->fillBuffer(bufferIndex);
    waveOutWrite((HWAVEOUT)engine->m_waveOut, header, sizeof(WAVEHDR));
}

void AudioEngine::update() {
    // The audio is handled by callbacks, but this can be used
    // for additional processing if needed
}

int AudioEngine::playSound(WAVData* wav, float volume, bool loop) {
    return m_mixer.play(wav, volume, loop);
}

void AudioEngine::stopSound(int id) {
    m_mixer.stop(id);
}

void AudioEngine::stopAll() {
    m_mixer.stopAll();
}

void AudioEngine::pauseSound(int id) {
    m_mixer.pause(id);
}

void AudioEngine::resumeSound(int id) {
    m_mixer.resume(id);
}

void AudioEngine::setMasterVolume(float volume) {
    m_mixer.setMasterVolume(volume);
}

float AudioEngine::getMasterVolume() const {
    return m_mixer.getMasterVolume();
}

void AudioEngine::setSoundVolume(int id, float volume) {
    m_mixer.setChannelVolume(id, volume);
}

void AudioEngine::setSoundPan(int id, float pan) {
    m_mixer.setChannelPan(id, pan);
}

void AudioEngine::setSoundPitch(int id, float pitch) {
    m_mixer.setChannelPitch(id, pitch);
}

bool AudioEngine::isPlaying(int id) const {
    return m_mixer.isPlaying(id);
}

} // namespace td
