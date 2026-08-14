// =============================================================================
// TD Engine - Skeletal Animation + GPU Skinning (Tier 2.4)
//
// Real implementation of skeletal animation:
//   - Skeleton: hierarchy of bones, each with a local pose (pos/rot/scale).
//   - AnimationClip: per-bone keyframe tracks (position, rotation, scale).
//   - Animator: samples an AnimationClip at time t, computes bone matrices,
//     uploads them as a uniform array for the vertex shader to do linear
//     blend skinning (LBS).
//   - Skin: per-vertex up to 4 bone indices + 4 weights.
// =============================================================================
#pragma once
#include "../core/math/vec3.h"
#include "../core/math/vec2.h"
#include "../core/math/mat4.h"
#include "../core/math/math.h"
#include "../core/logger.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace td {

// Build a rotation matrix from a quaternion (x, y, z, w).
// Standard formula. Inline so it doesn't add a separate .cpp.
inline Mat4 mat4FromQuaternion(float x, float y, float z, float w) {
    float xx = x * x, yy = y * y, zz = z * z;
    float xy = x * y, xz = x * z, yz = y * z;
    float wx = w * x, wy = w * y, wz = w * z;
    Mat4 m;
    m.m[0]  = 1.0f - 2.0f * (yy + zz);
    m.m[1]  = 2.0f * (xy + wz);
    m.m[2]  = 2.0f * (xz - wy);
    m.m[3]  = 0.0f;
    m.m[4]  = 2.0f * (xy - wz);
    m.m[5]  = 1.0f - 2.0f * (xx + zz);
    m.m[6]  = 2.0f * (yz + wx);
    m.m[7]  = 0.0f;
    m.m[8]  = 2.0f * (xz + wy);
    m.m[9]  = 2.0f * (yz - wx);
    m.m[10] = 1.0f - 2.0f * (xx + yy);
    m.m[11] = 0.0f;
    m.m[12] = 0.0f; m.m[13] = 0.0f; m.m[14] = 0.0f; m.m[15] = 1.0f;
    return m;
}

struct Bone {
    std::string name;
    int parent = -1;
    Mat4 inverseBindPose;
    Mat4 localTransform;
    Mat4 worldTransform;
};

template<typename T>
struct AnimationTrack {
    std::vector<float> times;
    std::vector<T> values;

    bool empty() const { return times.empty(); }
    float duration() const { return times.empty() ? 0.0f : times.back(); }

    bool sample(float t, T& out) const {
        if (times.empty()) return false;
        if (t <= times.front()) { out = values.front(); return true; }
        if (t >= times.back())  { out = values.back();  return true; }
        size_t lo = 0, hi = times.size() - 1;
        while (lo + 1 < hi) {
            size_t mid = (lo + hi) / 2;
            if (times[mid] <= t) lo = mid; else hi = mid;
        }
        float t0 = times[lo], t1 = times[hi];
        float alpha = (t1 - t0) > 1e-6f ? (t - t0) / (t1 - t0) : 0.0f;
        out = lerp(values[lo], values[hi], alpha);
        return true;
    }

    static T lerp(const T& a, const T& b, float t);
};

template<>
inline Vec3 AnimationTrack<Vec3>::lerp(const Vec3& a, const Vec3& b, float t) {
    return Vec3(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    );
}

// Vec4 used to store quaternion (x,y,z,w)
template<>
inline Vec4 AnimationTrack<Vec4>::lerp(const Vec4& a, const Vec4& b, float t) {
    Vec4 r(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t
    );
    float len = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
    if (len > 1e-6f) {
        float inv = 1.0f / len;
        r.x *= inv; r.y *= inv; r.z *= inv; r.w *= inv;
    }
    return r;
}

struct BoneAnimation {
    int boneIndex = -1;
    AnimationTrack<Vec3> positionTrack;
    AnimationTrack<Vec4> rotationTrack;
    AnimationTrack<Vec3> scaleTrack;
};

struct AnimationClip {
    std::string name;
    float duration = 0.0f;
    float ticksPerSecond = 30.0f;
    bool looping = true;
    std::vector<BoneAnimation> boneAnimations;
};

class Skeleton {
public:
    std::vector<Bone> bones;
    std::map<std::string, int> boneIndexByName;

    int boneCount() const { return (int)bones.size(); }

    int findBone(const std::string& name) const {
        auto it = boneIndexByName.find(name);
        return it != boneIndexByName.end() ? it->second : -1;
    }

    int addBone(const std::string& name, int parent, const Mat4& inverseBind) {
        int idx = (int)bones.size();
        Bone b;
        b.name = name;
        b.parent = parent;
        b.inverseBindPose = inverseBind;
        b.localTransform = Mat4::identity();
        b.worldTransform = Mat4::identity();
        bones.push_back(std::move(b));
        boneIndexByName[name] = idx;
        return idx;
    }

    void updateWorldTransforms() {
        for (size_t i = 0; i < bones.size(); i++) {
            Bone& b = bones[i];
            if (b.parent < 0) {
                b.worldTransform = b.localTransform;
            } else {
                b.worldTransform = bones[b.parent].worldTransform * b.localTransform;
            }
        }
    }

    std::vector<Mat4> computeSkinPalette() const {
        std::vector<Mat4> palette(bones.size());
        for (size_t i = 0; i < bones.size(); i++) {
            palette[i] = bones[i].worldTransform * bones[i].inverseBindPose;
        }
        return palette;
    }
};

class Animator {
public:
    Skeleton* skeleton = nullptr;
    const AnimationClip* currentClip = nullptr;
    float currentTime = 0.0f;
    float playbackSpeed = 1.0f;
    bool playing = false;

    const AnimationClip* previousClip = nullptr;
    float previousTime = 0.0f;
    float fadeTimer = 0.0f;
    float fadeDuration = 0.0f;

    void setSkeleton(Skeleton* sk) { skeleton = sk; }

    void play(const AnimationClip* clip, float fade = 0.2f) {
        if (currentClip && fade > 0.0f) {
            previousClip = currentClip;
            previousTime = currentTime;
            fadeTimer = 0.0f;
            fadeDuration = fade;
        } else {
            previousClip = nullptr;
            fadeDuration = 0.0f;
        }
        currentClip = clip;
        currentTime = 0.0f;
        playing = true;
    }

    void update(float dt) {
        if (!skeleton || !currentClip || !playing) return;

        currentTime += dt * playbackSpeed * currentClip->ticksPerSecond;
        if (currentClip->looping) {
            if (currentTime >= currentClip->duration) {
                currentTime = std::fmod(currentTime, currentClip->duration);
            }
        } else if (currentTime >= currentClip->duration) {
            currentTime = currentClip->duration;
            playing = false;
        }

        if (previousClip && fadeDuration > 0.0f) {
            fadeTimer += dt;
            previousTime += dt * previousClip->ticksPerSecond;
            if (previousTime >= previousClip->duration) {
                previousTime = std::fmod(previousTime, previousClip->duration);
            }
            if (fadeTimer >= fadeDuration) {
                previousClip = nullptr;
            }
        }

        applyClip(currentClip, currentTime, 1.0f);
        if (previousClip) {
            float fadeAlpha = fadeTimer / fadeDuration;
            applyClip(previousClip, previousTime, 1.0f - fadeAlpha);
        }

        skeleton->updateWorldTransforms();
    }

    std::vector<Mat4> getSkinPalette() const {
        if (!skeleton) return {};
        return skeleton->computeSkinPalette();
    }

private:
    void applyClip(const AnimationClip* clip, float t, float w) {
        for (const auto& ba : clip->boneAnimations) {
            if (ba.boneIndex < 0 || ba.boneIndex >= (int)skeleton->bones.size()) continue;
            Bone& b = skeleton->bones[ba.boneIndex];

            Vec3 pos(0, 0, 0);
            Vec4 rot(0, 0, 0, 1);
            Vec3 scale(1, 1, 1);
            ba.positionTrack.sample(t, pos);
            ba.rotationTrack.sample(t, rot);
            ba.scaleTrack.sample(t, scale);

            Mat4 T = Mat4::translate(pos);
            Mat4 R = mat4FromQuaternion(rot.x, rot.y, rot.z, rot.w);
            Mat4 S = Mat4::scale(scale);
            Mat4 local = T * R * S;

            if (w >= 1.0f) {
                b.localTransform = local;
            } else {
                for (int i = 0; i < 16; i++) {
                    b.localTransform.m[i] = b.localTransform.m[i] * (1.0f - w) + local.m[i] * w;
                }
            }
        }
    }
};

struct SkinnedVertex {
    Vec3 position;
    Vec3 normal;
    Vec2 texcoord;
    uint8_t boneIndices[4] = {0, 0, 0, 0};
    float boneWeights[4] = {0, 0, 0, 0};

    void addBone(uint8_t index, float weight) {
        int minIdx = 0;
        for (int i = 1; i < 4; i++) {
            if (boneWeights[i] < boneWeights[minIdx]) minIdx = i;
        }
        if (weight > boneWeights[minIdx]) {
            boneIndices[minIdx] = index;
            boneWeights[minIdx] = weight;
        }
    }

    void normalizeWeights() {
        float sum = 0.0f;
        for (int i = 0; i < 4; i++) sum += boneWeights[i];
        if (sum > 1e-6f) {
            float inv = 1.0f / sum;
            for (int i = 0; i < 4; i++) boneWeights[i] *= inv;
        }
    }
};

inline const char* kSkinningVertexShaderGLSL = R"GLSL(
#version 300 es
precision highp float;

layout(location=0) in vec3 a_position;
layout(location=1) in vec3 a_normal;
layout(location=2) in vec2 a_texcoord;
layout(location=3) in vec4 a_boneIndices;
layout(location=4) in vec4 a_boneWeights;

uniform mat4 u_model;
uniform mat4 u_viewProj;
uniform mat4 u_skinPalette[64];

out vec3 v_normal;
out vec2 v_texcoord;

void main() {
    ivec4 idx = ivec4(a_boneIndices);
    mat4 skin =
        u_skinPalette[idx.x] * a_boneWeights.x +
        u_skinPalette[idx.y] * a_boneWeights.y +
        u_skinPalette[idx.z] * a_boneWeights.z +
        u_skinPalette[idx.w] * a_boneWeights.w;
    vec4 skinnedPos = skin * vec4(a_position, 1.0);
    gl_Position = u_viewProj * u_model * skinnedPos;
    v_normal = mat3(skin) * a_normal;
    v_texcoord = a_texcoord;
}
)GLSL";

} // namespace td
