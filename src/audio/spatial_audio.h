// =============================================================================
// TD Engine - 3D Positional Audio (Task wave1-physaudio)
//
// Adds a 3D spatialization layer on top of the existing 2D `Mixer`. The 2D
// mixer is unchanged (mixer.h is NOT modified). This file declares:
//
//   - AudioListener       : position + orientation + velocity (for Doppler)
//   - SpatialSource       : wraps a WAVData* with world position, distance
//                           attenuation, cone, Doppler factor, reverb send.
//   - SchroederReverb     : 4 parallel comb filters + 2 series allpass.
//   - SpatialMixer        : mixes SpatialSources with HRTF-lite panning +
//                           distance attenuation + Doppler pitch shift +
//                           reverb send. Output is stereo int16 interleaved,
//                           same format as the 2D Mixer so both can feed the
//                           same audio device buffer.
//
// HRTF-lite: we don't ship a full HRTF database. Instead we use:
//   - Constant-power pan based on the angle between listener-forward and
//     source-direction (azimuth).
//   - Distance attenuation (linear or logarithmic).
//   - Interaural level difference (ILD): the ear opposite the source gets
//     an extra -6 dB attenuation at 90° azimuth, linearly scaled for
//     intermediate angles. This is the dominant localization cue for
//     high-frequency content; full HRTF would also model interaural time
//     difference (ITD), but ITD is sub-millisecond and not audible through
//     cheap headphones at typical game audio latencies.
//
// Doppler: pitchRatio = (c + listenerVel·dir) / (c + sourceVel·dir), clamped
// to a sane range. dir is the unit vector from source to listener.
//
// Reverb (Schroeder): 4 parallel comb filters (lowpass-damped feedback
// delay) summed, then 2 series allpass filters. Room size sets the comb
// delay lengths and feedback; reverb level is the wet/dry mix.
//
// C API: extern "C" functions td_audio_set_listener / td_audio_play_3d /
// td_audio_set_reverb are declared at the bottom. A future task will wire
// them into wasm/emscripten_main.cpp + add them to the EXPORTED_FUNCTIONS
// list in Makefile/CMakeLists.
// =============================================================================
#pragma once
#include "wav_loader.h"
#include "../core/math/vec3.h"
#include "../core/math/math.h"
#include <cstdint>

namespace td {

// -----------------------------------------------------------------------------
// AudioListener — the "ears" of the world. Usually attached to the camera.
// -----------------------------------------------------------------------------
struct AudioListener {
    Vec3 position  = {0, 0, 0};
    Vec3 forward   = {0, 0, -1};   // normalized
    Vec3 up        = {0, 1, 0};    // normalized
    Vec3 velocity  = {0, 0, 0};    // for Doppler (m/s)
};

// -----------------------------------------------------------------------------
// Rolloff curve for distance attenuation.
// -----------------------------------------------------------------------------
enum class RolloffCurve : uint8_t {
    Linear,        // gain = clamp(1 - (d - min) / (max - min), 0, 1)
    Logarithmic    // gain = 1 / (1 + rolloff * max(d - min, 0))
};

// -----------------------------------------------------------------------------
// SpatialSource — a 3D sound source.
// -----------------------------------------------------------------------------
struct SpatialSource {
    WAVData* wav = nullptr;
    Vec3     position    = {0, 0, 0};
    Vec3     velocity    = {0, 0, 0};   // for Doppler (m/s)
    Vec3     forward     = {0, 0, -1};  // cone direction (normalized)

    // Distance attenuation.
    float        minDistance   = 1.0f;
    float        maxDistance   = 50.0f;
    float        rolloffFactor = 1.0f;
    RolloffCurve rolloffCurve  = RolloffCurve::Linear;

    // Directional cone (for megaphone / spotlight sounds).
    // Inner cone = full gain. Outer cone = coneOuterGain. Between = lerped.
    // Angles are in radians, measured from the cone's forward axis.
    float coneInnerAngle = TD_TAU;   // default = full sphere (no cone)
    float coneOuterAngle = TD_TAU;
    float coneOuterGain  = 0.0f;

    // Reverb send level (0 = dry, 1 = fully wet).
    float reverbSend = 0.0f;

    // Playback state.
    float  pitch      = 1.0f;
    float  volume     = 1.0f;
    bool   playing    = false;
    bool   looping    = false;
    bool   paused     = false;
    double samplePos = 0.0;   // position in source samples (double for accurate pitch)
    int    id         = -1;
};

// -----------------------------------------------------------------------------
// SchroederReverb — classic 4-comb + 2-allpass reverb. Good enough for game
// environments without the cost of a convolution reverb.
// -----------------------------------------------------------------------------
class SchroederReverb {
public:
    void init(int sampleRate, float roomSize = 0.5f, float reverbLevel = 0.3f);

    void setRoomSize(float roomSize);     // 0..1, sets delay lengths + feedback
    void setReverbLevel(float level);     // 0..1, wet/dry mix

    // Process a mono input sample, returning the wet (reverberated) sample.
    // Dry is mixed in by the caller.
    float process(float input);

    // Clear internal state (use on room change to avoid leftover tail).
    void clear();

    int   getSampleRate()  const { return m_sampleRate; }
    float getRoomSize()    const { return m_roomSize; }
    float getReverbLevel() const { return m_reverbLevel; }

private:
    // Comb filter with one-pole lowpass damping (Freeverb-style).
    struct Comb {
        static const int MAX_DELAY = 8192;
        float buf[MAX_DELAY] = {};
        int   writeIdx = 0;
        int   delay    = 1;
        float feedback = 0.5f;     // gain around the loop
        float damp     = 0.0f;     // lowpass coefficient (0 = no damp, 1 = max)
        float last     = 0.0f;     // lowpass state

        void clear() {
            for (int i = 0; i < MAX_DELAY; i++) buf[i] = 0.0f;
            writeIdx = 0;
            last = 0.0f;
        }
        float process(float in) {
            float out = buf[writeIdx];
            // One-pole lowpass in the feedback path (smoothes the tail).
            last = out + (1.0f - damp) * (last - out);
            buf[writeIdx] = in + last * feedback;
            writeIdx = (writeIdx + 1) % delay;
            return out;
        }
    };

    // Allpass filter (diffuses the comb tail).
    struct Allpass {
        static const int MAX_DELAY = 4096;
        float buf[MAX_DELAY] = {};
        int   writeIdx = 0;
        int   delay    = 1;
        float feedback = 0.5f;

        void clear() {
            for (int i = 0; i < MAX_DELAY; i++) buf[i] = 0.0f;
            writeIdx = 0;
        }
        float process(float in) {
            float bufout = buf[writeIdx];
            float out = -in + bufout;
            buf[writeIdx] = in + bufout * feedback;
            writeIdx = (writeIdx + 1) % delay;
            return out;
        }
    };

    Comb     m_combs[4];
    Allpass  m_allpasses[2];
    float    m_roomSize    = 0.5f;
    float    m_reverbLevel = 0.3f;
    int      m_sampleRate  = 44100;
};

// -----------------------------------------------------------------------------
// SpatialMixer — mixes up to MAX_SOURCES spatial sources with HRTF-lite
// spatialization + Schroeder reverb. Output is stereo int16 interleaved
// (same format as the 2D Mixer), so both can be summed into the same device
// buffer.
// -----------------------------------------------------------------------------
class SpatialMixer {
public:
    static const int MAX_SOURCES = 32;

    void init(int sampleRate = 44100, int channels = 2);

    // Mix all playing spatial sources + reverb into the output buffer.
    // outputSamples = number of int16 samples (NOT frames). For stereo,
    // frames = outputSamples / 2.
    void mix(int16_t* output, int outputSamples);

    // Listener.
    void setListener(const AudioListener& listener) { m_listener = listener; }
    const AudioListener& getListener() const { return m_listener; }

    // Source management. play() returns a source id (>0) or -1 if no slots.
    int  play(WAVData* wav, const Vec3& pos,
               float minDist, float maxDist, float rolloff, bool loop);
    void stop(int sourceId);
    void stopAll();
    SpatialSource* getSource(int sourceId);

    // Reverb.
    void setReverb(float roomSize, float reverbLevel);
    SchroederReverb& getReverb() { return m_reverb; }

    // Master volume (applies to spatial mix only; the 2D Mixer has its own).
    void setMasterVolume(float v) { m_masterVolume = clamp(v, 0.0f, 2.0f); }
    float getMasterVolume() const { return m_masterVolume; }

    // Doppler / speed-of-sound tuning.
    void setSpeedOfSound(float s) { m_speedOfSound = maxF(1.0f, s); }
    void setDopplerFactor(float f) { m_dopplerFactor = maxF(0.0f, f); }

    // Compute spatialization for a source given the current listener.
    // outLeftGain, outRightGain are linear gains [0..1+] (can exceed 1 for
    // sources between listener and ear). outPitchRatio is the Doppler-shifted
    // playback ratio. Exposed publicly so tests + gameplay code can verify
    // spatialization values without running the full mix loop.
    void spatialize(const SpatialSource& src,
                     float& outLeftGain, float& outRightGain,
                     float& outPitchRatio) const;

    int  getOutputSampleRate() const { return m_outputSampleRate; }
    int  getOutputChannels()   const { return m_outputChannels; }
    int  getActiveSources() const;

private:
    SpatialSource   m_sources[MAX_SOURCES];
    AudioListener   m_listener;
    SchroederReverb m_reverb;
    float           m_masterVolume   = 1.0f;
    float           m_speedOfSound   = 343.0f;   // m/s
    float           m_dopplerFactor  = 1.0f;
    int             m_outputSampleRate = 44100;
    int             m_outputChannels   = 2;
    int             m_nextId          = 1;

    // Read a normalized [-1, 1] sample from the WAV at the given source-frame
    // position with linear interpolation. `channel` is 0 (left/mono) or 1.
    float readSampleInterp(const WAVData* wav, double sampleIndex,
                            int channel) const;
};

} // namespace td

// -----------------------------------------------------------------------------
// C API exports. These are extern "C" so they can be called from the WASM
// bridge (wasm/emscripten_main.cpp) without name mangling. They use a
// process-global SpatialMixer instance so the caller doesn't have to pass
// a handle around.
//
// A future task will:
//   1. Add these to the EXPORTED_FUNCTIONS list in Makefile + CMakeLists.
//   2. Wire them up in wasm/emscripten_main.cpp (create a global
//      td::SpatialMixer alongside the existing td::Mixer, call its mix()
//      from td_fill_audio_buffer).
// -----------------------------------------------------------------------------
extern "C" {
    // Set the audio listener's position + orientation. forward/up need not
    // be normalized (the implementation normalizes them).
    void td_audio_set_listener(float posX, float posY, float posZ,
                                float forwardX, float forwardY, float forwardZ,
                                float upX, float upY, float upZ);

    // Play a 3D sound. soundId is an index into the WASM-side sound table
    // (set up by the future task that wires this into the bridge). For
    // standalone builds, soundId is interpreted as a WAVData* cast to int.
    // Returns a source id (>0) or -1 on failure.
    int  td_audio_play_3d(uint32_t soundId,
                           float posX, float posY, float posZ,
                           float minDist, float maxDist,
                           float rolloff, int loop);

    // Set global reverb parameters.
    void td_audio_set_reverb(float roomSize, float reverbLevel);
}
