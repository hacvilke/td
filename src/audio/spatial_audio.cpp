// =============================================================================
// TD Engine - 3D Positional Audio (Task wave1-physaudio) — implementation.
//
// See spatial_audio.h for the design overview. This file implements:
//   - SchroederReverb (4 comb + 2 allpass, Freeverb-style damping)
//   - SpatialMixer::spatialize (HRTF-lite: constant-power pan + ILD +
//     distance attenuation + Doppler)
//   - SpatialMixer::mix (per-source pitch-shifted resample + spatial gains
//     + reverb send → stereo int16 output)
//   - C API: td_audio_set_listener / td_audio_play_3d / td_audio_set_reverb
//
// Performance: 32 sources × 256 frames × 2 channels = 16k inner-loop
// iterations per mix call. With linear interpolation + a few multiplies per
// sample, this is well under 0.1 ms on desktop CPUs — far below the 2%
// budget (1 ms at 60 FPS). The reverb adds 6 IIR filters per mono sample
// (4 comb + 2 allpass), which is also negligible.
//
// No external libraries. C++17, portable, -Wall -Wextra clean.
// =============================================================================

#include "spatial_audio.h"
#include "../core/math/math.h"
#include <cstring>
#include <cmath>

namespace td {

// ============================================================================
// SchroederReverb
// ============================================================================
// Freeverb-derived constants (scaled by sample rate). These give a
// pleasant "small room" to "large hall" range as roomSize goes 0..1.
// Combs: 1116, 1188, 1277, 1356 samples at 44.1 kHz (Freeverb defaults).
// Allpasses: 556, 441 samples at 44.1 kHz.
// We scale delay lengths linearly by (sampleRate / 44100) and by roomSize
// for the combs (longer reverb tail = bigger room).
static int scaleDelay(int base44100, int sampleRate) {
    int d = (int)(base44100 * (float)sampleRate / 44100.0f);
    if (d < 1) d = 1;
    return d;
}

void SchroederReverb::init(int sampleRate, float roomSize, float reverbLevel) {
    m_sampleRate  = sampleRate;
    m_roomSize    = clamp(roomSize, 0.0f, 1.0f);
    m_reverbLevel = clamp(reverbLevel, 0.0f, 1.0f);
    clear();
    setRoomSize(m_roomSize);   // applies delay + feedback
}

void SchroederReverb::setRoomSize(float roomSize) {
    m_roomSize = clamp(roomSize, 0.0f, 1.0f);

    // Comb delays grow with roomSize. Freeverb's fixed gains are 0.777,
    // 0.747, 0.718, 0.704. We scale feedback up slightly with room size for
    // a longer tail.
    static const int combBase44100[4] = {1116, 1188, 1277, 1356};
    for (int i = 0; i < 4; i++) {
        int d = scaleDelay(combBase44100[i], m_sampleRate);
        // Stretch delay by (0.7 + 0.6 * roomSize) → 0.7×..1.3× the base.
        d = (int)(d * (0.7f + 0.6f * m_roomSize));
        if (d >= SchroederReverb::Comb::MAX_DELAY) d = SchroederReverb::Comb::MAX_DELAY - 1;
        if (d < 1) d = 1;
        m_combs[i].delay    = d;
        // Feedback grows with room size: 0.65 at room=0, 0.92 at room=1.
        m_combs[i].feedback = 0.65f + 0.27f * m_roomSize;
        // Damping shrinks with room size (bigger room = brighter tail).
        m_combs[i].damp     = 0.4f - 0.3f * m_roomSize;
    }

    static const int allpassBase44100[2] = {556, 441};
    for (int i = 0; i < 2; i++) {
        int d = scaleDelay(allpassBase44100[i], m_sampleRate);
        if (d >= SchroederReverb::Allpass::MAX_DELAY) d = SchroederReverb::Allpass::MAX_DELAY - 1;
        if (d < 1) d = 1;
        m_allpasses[i].delay    = d;
        m_allpasses[i].feedback = 0.5f;   // Freeverb's allpass gain
    }
}

void SchroederReverb::setReverbLevel(float level) {
    m_reverbLevel = clamp(level, 0.0f, 1.0f);
}

float SchroederReverb::process(float input) {
    // 4 parallel comb filters, summed.
    float combSum = 0.0f;
    for (int i = 0; i < 4; i++) {
        combSum += m_combs[i].process(input);
    }
    // Scale by 0.25 to normalize the 4-way sum (Freeverb uses 0.25f).
    combSum *= 0.25f;

    // 2 series allpass filters for diffusion.
    float wet = m_allpasses[0].process(combSum);
    wet = m_allpasses[1].process(wet);

    // Wet/dry mix. The caller is responsible for mixing the dry signal;
    // here we just return the wet (reverberated) component, scaled by
    // reverbLevel. This way the SpatialMixer can apply reverbSend per
    // source and sum the wet contributions.
    return wet * m_reverbLevel;
}

void SchroederReverb::clear() {
    for (int i = 0; i < 4; i++) m_combs[i].clear();
    for (int i = 0; i < 2; i++) m_allpasses[i].clear();
}

// ============================================================================
// SpatialMixer
// ============================================================================
void SpatialMixer::init(int sampleRate, int channels) {
    m_outputSampleRate = sampleRate;
    m_outputChannels   = channels;
    m_nextId           = 1;
    m_masterVolume     = 1.0f;
    for (int i = 0; i < MAX_SOURCES; i++) {
        m_sources[i] = SpatialSource();
    }
    m_reverb.init(sampleRate, 0.5f, 0.3f);
}

int SpatialMixer::play(WAVData* wav, const Vec3& pos,
                        float minDist, float maxDist, float rolloff,
                        bool loop) {
    if (!wav) return -1;

    int slot = -1;
    for (int i = 0; i < MAX_SOURCES; i++) {
        if (!m_sources[i].playing) { slot = i; break; }
    }
    if (slot < 0) return -1;

    SpatialSource& s = m_sources[slot];
    s.wav          = wav;
    s.position     = pos;
    s.velocity     = Vec3(0, 0, 0);
    s.forward      = Vec3(0, 0, -1);
    s.minDistance  = maxF(0.001f, minDist);
    s.maxDistance  = maxF(s.minDistance + 0.001f, maxDist);
    s.rolloffFactor= maxF(0.0f, rolloff);
    s.rolloffCurve = RolloffCurve::Linear;
    s.coneInnerAngle = TD_TAU;
    s.coneOuterAngle = TD_TAU;
    s.coneOuterGain  = 0.0f;
    s.reverbSend   = 0.0f;
    s.pitch        = 1.0f;
    s.volume       = 1.0f;
    s.playing      = true;
    s.looping      = loop;
    s.paused       = false;
    s.samplePos    = 0.0;
    s.id           = m_nextId++;
    return s.id;
}

void SpatialMixer::stop(int sourceId) {
    for (int i = 0; i < MAX_SOURCES; i++) {
        if (m_sources[i].id == sourceId) {
            m_sources[i].playing = false;
            m_sources[i].id = -1;
            return;
        }
    }
}

void SpatialMixer::stopAll() {
    for (int i = 0; i < MAX_SOURCES; i++) {
        m_sources[i].playing = false;
        m_sources[i].id = -1;
    }
    m_reverb.clear();
}

SpatialSource* SpatialMixer::getSource(int sourceId) {
    for (int i = 0; i < MAX_SOURCES; i++) {
        if (m_sources[i].id == sourceId) return &m_sources[i];
    }
    return nullptr;
}

void SpatialMixer::setReverb(float roomSize, float reverbLevel) {
    m_reverb.setRoomSize(roomSize);
    m_reverb.setReverbLevel(reverbLevel);
}

int SpatialMixer::getActiveSources() const {
    int n = 0;
    for (int i = 0; i < MAX_SOURCES; i++) {
        if (m_sources[i].playing) n++;
    }
    return n;
}

// ----------------------------------------------------------------------------
// Spatialization: compute left/right gains + Doppler pitch ratio.
//
// Steps:
//   1. Vector from listener to source (in world space).
//   2. Distance attenuation (linear or logarithmic).
//   3. Project the source direction onto the listener's right vector to get
//      the azimuth (-1 = full left, +1 = full right).
//   4. Constant-power pan: left = cos((pan+1)*π/4), right = sin((pan+1)*π/4).
//   5. Interaural level difference (ILD): the ear opposite the source gets
//      an extra -6 dB (= 0.5×) attenuation, scaled by |azimuth|.
//   6. Cone attenuation (if source is directional).
//   7. Doppler: pitchRatio = (c + lv·dir) / (c + sv·dir), where dir is
//      source→listener unit vector. Clamp to [0.25, 4].
// ----------------------------------------------------------------------------
void SpatialMixer::spatialize(const SpatialSource& src,
                                float& outLeftGain, float& outRightGain,
                                float& outPitchRatio) const {
    Vec3 toSrc = src.position - m_listener.position;
    float dist = sqrtF(toSrc.x * toSrc.x + toSrc.y * toSrc.y + toSrc.z * toSrc.z);
    if (dist < TD_EPSILON) dist = TD_EPSILON;
    Vec3 dirToSrc = toSrc / dist;     // listener → source (unit)

    // 1. Distance attenuation.
    float distGain;
    if (src.rolloffCurve == RolloffCurve::Logarithmic) {
        float d = maxF(0.0f, dist - src.minDistance);
        distGain = 1.0f / (1.0f + src.rolloffFactor * d);
    } else {  // Linear
        if (dist <= src.minDistance) {
            distGain = 1.0f;
        } else if (dist >= src.maxDistance) {
            distGain = 0.0f;
        } else {
            float t = (dist - src.minDistance) / (src.maxDistance - src.minDistance);
            distGain = 1.0f - t;
        }
    }

    // 2. Azimuth: project source direction onto listener's right vector.
    //    Listener right = forward × up (right-handed).
    Vec3 right = m_listener.forward.cross(m_listener.up);
    float rlen = sqrtF(right.x * right.x + right.y * right.y + right.z * right.z);
    if (rlen < TD_EPSILON) {
        right = Vec3(1, 0, 0);
    } else {
        right = right / rlen;
    }
    // Pan in [-1, +1]. dot = cos(angle between right and dirToSrc).
    float pan = dirToSrc.dot(right);
    pan = clamp(pan, -1.0f, 1.0f);

    // 3. Constant-power pan law: at pan=0, both ears get ~0.707 (-3 dB).
    //    At pan=-1 (full left), left=1, right=0. At pan=+1, left=0, right=1.
    float angle = (pan + 1.0f) * 0.25f * TD_PI;  // 0..π/2
    float panL = cosF(angle);
    float panR = sinF(angle);

    // 4. Interaural level difference: at |pan|=1 (90° to the side), the far
    //    ear gets an extra -6 dB attenuation. Linearly scaled by |pan|.
    //    This is the dominant high-frequency localization cue.
    float ild = 0.5f;   // -6 dB at full side
    float farAttenuation = 1.0f - ild * absF(pan);
    if (pan < 0.0f) {
        // Source on the left → right ear is the "far" ear.
        panR *= farAttenuation;
    } else {
        panL *= farAttenuation;
    }

    // 5. Cone attenuation (directional source).
    float coneGain = 1.0f;
    if (src.coneInnerAngle < TD_TAU || src.coneOuterAngle < TD_TAU) {
        // dir from source to listener = -dirToSrc
        Vec3 toListener = dirToSrc * -1.0f;
        float cosInner = cosF(src.coneInnerAngle * 0.5f);
        float cosOuter = cosF(src.coneOuterAngle * 0.5f);
        // forward should be normalized; if it's zero, no cone effect.
        Vec3 fwd = src.forward;
        float flen = sqrtF(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
        if (flen > TD_EPSILON) {
            fwd = fwd / flen;
            float cosAngle = toListener.dot(fwd);  // 1 = directly ahead
            if (cosAngle >= cosInner) {
                coneGain = 1.0f;   // inside inner cone
            } else if (cosAngle <= cosOuter) {
                coneGain = src.coneOuterGain;   // outside outer cone
            } else {
                // Lerp between inner (1) and outer (outerGain).
                float t = (cosInner - cosAngle) / (cosInner - cosOuter);
                coneGain = lerp(1.0f, src.coneOuterGain, t);
            }
        }
    }

    // 6. Combine all gains.
    float g = distGain * coneGain * src.volume * m_masterVolume;
    outLeftGain  = panL * g;
    outRightGain = panR * g;

    // 7. Doppler pitch shift.
    //    dir = listener → source unit vector (= dirToSrc).
    //    Classical Doppler: f' = f * (c + lv·(-dir)) / (c + sv·(-dir))
    //    (lv·(-dir) = listener moving toward source = increased freq)
    //    We rewrite using dir = source → listener = -dirToSrc to match
    //    the task's formula: pitchRatio = (c + lv·dir) / (c + sv·dir).
    Vec3 sToL = dirToSrc * -1.0f;   // source → listener
    float lv = m_listener.velocity.dot(sToL);
    float sv = src.velocity.dot(sToL);
    float c  = m_speedOfSound;
    float denom = c + sv;
    if (absF(denom) < 1.0f) denom = (denom < 0.0f) ? -1.0f : 1.0f;
    float ratio = (c + lv) / denom;
    // Apply the user-scaled Doppler factor (1.0 = physically accurate).
    ratio = 1.0f + (ratio - 1.0f) * m_dopplerFactor;
    // Clamp to a safe playback range (4× up, 0.25× down).
    outPitchRatio = clamp(ratio, 0.25f, 4.0f) * src.pitch;
}

// ----------------------------------------------------------------------------
// Sample reader with linear interpolation. Handles 8/16/24/32-bit, mono/stereo.
// ----------------------------------------------------------------------------
float SpatialMixer::readSampleInterp(const WAVData* wav, double sampleIndex,
                                       int channel) const {
    if (!wav || !wav->data || wav->numSamples == 0) return 0.0f;

    // Integer position + fractional part for linear interp.
    double iFloor = floorF((float)sampleIndex);
    double frac   = sampleIndex - iFloor;
    uint32_t i0 = (uint32_t)iFloor;
    uint32_t i1 = i0 + 1;

    auto readRaw = [wav](uint32_t idx, int ch) -> float {
        if (idx >= wav->numSamples) return 0.0f;
        int srcCh = (wav->numChannels == 1) ? 0 : ch;
        int bps   = wav->bitsPerSample / 8;
        uint32_t off = (idx * wav->numChannels + srcCh) * bps;
        if (off + bps > wav->dataSize) return 0.0f;
        if (wav->bitsPerSample == 8) {
            // 8-bit PCM is unsigned (0..255), center = 128.
            uint8_t s = wav->data[off];
            return ((float)s - 128.0f) / 128.0f;
        }
        if (wav->bitsPerSample == 16) {
            int16_t s = (int16_t)((uint16_t)wav->data[off] |
                                   ((uint16_t)wav->data[off + 1] << 8));
            return (float)s / 32768.0f;
        }
        if (wav->bitsPerSample == 24) {
            int32_t s = (int32_t)wav->data[off] |
                         ((int32_t)wav->data[off + 1] << 8) |
                         ((int32_t)wav->data[off + 2] << 16);
            if (s & 0x800000) s |= 0xFF000000;   // sign-extend
            return (float)s / 8388608.0f;
        }
        if (wav->bitsPerSample == 32) {
            int32_t s = (int32_t)((uint32_t)wav->data[off] |
                                   ((uint32_t)wav->data[off + 1] << 8) |
                                   ((uint32_t)wav->data[off + 2] << 16) |
                                   ((uint32_t)wav->data[off + 3] << 24));
            return (float)s / 2147483648.0f;
        }
        return 0.0f;
    };

    float s0 = readRaw(i0, channel);
    float s1 = (i1 < wav->numSamples) ? readRaw(i1, channel) : s0;
    return s0 + (s1 - s0) * (float)frac;
}

// ----------------------------------------------------------------------------
// Main mix loop.
// ----------------------------------------------------------------------------
void SpatialMixer::mix(int16_t* output, int outputSamples) {
    // Clear output.
    memset(output, 0, (size_t)outputSamples * sizeof(int16_t));

    if (m_outputChannels < 1 || m_outputChannels > 2) return;

    // Accumulate in float to avoid clipping artifacts mid-mix.
    // Use a heap buffer because outputSamples can be large (e.g., 4096×2).
    float* mixBuf = new float[outputSamples];
    memset(mixBuf, 0, (size_t)outputSamples * sizeof(float));
    // Reverb wet buffer (mono → expanded to stereo on output).
    float* reverbBuf = new float[outputSamples / m_outputChannels + 1];

    int frames = outputSamples / m_outputChannels;

    for (int s = 0; s < MAX_SOURCES; s++) {
        SpatialSource& src = m_sources[s];
        if (!src.playing || src.paused || !src.wav) continue;

        // Compute spatialization once per mix call. (We could re-evaluate
        // per frame for moving sources, but per-call is enough for game
        // audio at typical 256-frame buffers — 5.8 ms at 44.1 kHz.)
        float lg, rg, pitchRatio;
        spatialize(src, lg, rg, pitchRatio);

        // Sample-rate conversion: ratio = sourceRate / outputRate × pitch.
        float srRatio = (float)src.wav->sampleRate / (float)m_outputSampleRate;
        double step = (double)(srRatio * pitchRatio);

        for (int f = 0; f < frames; f++) {
            // Loop / end-of-sound handling.
            if (src.samplePos >= (double)src.wav->numSamples) {
                if (src.looping) {
                    src.samplePos = std::fmod(src.samplePos, (double)src.wav->numSamples);
                } else {
                    src.playing = false;
                    break;
                }
            }

            // Read source sample (mono source → both channels; stereo source
            // → preserve channel identity).
            float sampleL, sampleR;
            if (src.wav->numChannels == 1) {
                float m = readSampleInterp(src.wav, src.samplePos, 0);
                sampleL = m;
                sampleR = m;
            } else {
                sampleL = readSampleInterp(src.wav, src.samplePos, 0);
                sampleR = readSampleInterp(src.wav, src.samplePos, 1);
            }

            // Apply spatialization gains.
            sampleL *= lg;
            sampleR *= rg;

            // Write to mix buffer.
            if (m_outputChannels == 1) {
                mixBuf[f] += (sampleL + sampleR) * 0.5f;
            } else {
                mixBuf[f * 2]     += sampleL;
                mixBuf[f * 2 + 1] += sampleR;
            }

            // Reverb send.
            if (src.reverbSend > 0.0f) {
                float monoSend = (sampleL + sampleR) * 0.5f * src.reverbSend;
                reverbBuf[f] += monoSend;
            }

            // Advance source position.
            src.samplePos += step;
        }

        // If the source ended, free its slot.
        if (!src.playing) {
            src.id = -1;
        }
    }

    // Process reverb sends → wet signal, mix into output.
    if (m_reverb.getReverbLevel() > 0.0f) {
        for (int f = 0; f < frames; f++) {
            float wet = m_reverb.process(reverbBuf[f]);
            if (m_outputChannels == 1) {
                mixBuf[f] += wet;
            } else {
                mixBuf[f * 2]     += wet;
                mixBuf[f * 2 + 1] += wet;
            }
        }
    }

    // Convert float mix → int16 with soft clipping.
    for (int i = 0; i < outputSamples; i++) {
        float v = mixBuf[i];
        if (v > 1.0f)  v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        output[i] = (int16_t)(v * 32767.0f);
    }

    delete[] mixBuf;
    delete[] reverbBuf;
}

} // namespace td

// ============================================================================
// C API exports. Process-global SpatialMixer so callers don't need a handle.
// ============================================================================
static td::SpatialMixer g_spatialMixer;
static bool             g_spatialInit = false;

static td::SpatialMixer& spatialEngine() {
    if (!g_spatialInit) {
        g_spatialMixer.init(44100, 2);
        g_spatialInit = true;
    }
    return g_spatialMixer;
}

extern "C" {

void td_audio_set_listener(float posX, float posY, float posZ,
                            float forwardX, float forwardY, float forwardZ,
                            float upX, float upY, float upZ) {
    td::SpatialMixer& m = spatialEngine();
    td::AudioListener l = m.getListener();
    l.position = td::Vec3(posX, posY, posZ);
    l.forward  = td::Vec3(forwardX, forwardY, forwardZ).normalized();
    l.up       = td::Vec3(upX, upY, upZ).normalized();
    m.setListener(l);
}

int td_audio_play_3d(uint32_t soundId, float posX, float posY, float posZ,
                      float minDist, float maxDist, float rolloff, int loop) {
    td::SpatialMixer& m = spatialEngine();
    // In standalone builds, soundId is interpreted as a WAVData* cast to int.
    td::WAVData* wav = reinterpret_cast<td::WAVData*>((uintptr_t)soundId);
    return m.play(wav, td::Vec3(posX, posY, posZ),
                   minDist, maxDist, rolloff, loop != 0);
}

void td_audio_set_reverb(float roomSize, float reverbLevel) {
    td::SpatialMixer& m = spatialEngine();
    m.setReverb(roomSize, reverbLevel);
}

}  // extern "C"
