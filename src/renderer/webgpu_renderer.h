// =============================================================================
// TD Engine - WebGPU Renderer Path (Tier 4)
//
// WebGPU is the modern browser graphics API (successor to WebGL2). It offers:
//   - Lower overhead (explicit command encoding, no implicit state).
//   - Compute shaders (post-processing, particle systems, GPU culling).
//   - Better multithreading (Dawn / wgpu-native support worker threads).
//   - Bind groups (replaces uniform arrays, more flexible).
//
// This file provides a scaffolding for a WebGPU renderer that can run
// alongside the existing WebGL2 renderer. The engine picks the best
// available backend at startup (WebGPU if available, else WebGL2).
//
// Status: SCAFFOLDING. The interface is defined; the actual WebGPU calls
// are stubbed out (return false / no-op). Real WebGPU integration would
// require either Dawn (C++) or wgpu-native (Rust/C) — both external deps,
// which violates the "zero external libraries" principle. Instead, on
// WASM, the browser's `navigator.gpu` is accessed via Emscripten's
// emscripten_webgpu_* API (which is just JS interop, no external dep).
// =============================================================================
#pragma once
#include "../core/logger.h"
#include "../core/math/mat4.h"
#include "../core/math/vec3.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace td {
namespace webgpu {

enum class Backend : uint8_t {
    WebGPU,    // Native WebGPU (browser via navigator.gpu, or Dawn/wgpu-native)
    WebGL2,    // Fallback
    Auto,      // Pick the best available at runtime.
};

// Opaque handles — in a real impl these would be wrappers around WGPUDevice,
// WGPUSwapChain, WGPUBuffer, etc.
struct Device;
struct SwapChain;
struct Buffer;
struct Pipeline;
struct Texture;

struct DeviceCapabilities {
    bool supportsWebGPU = false;
    bool supportsComputeShaders = false;
    bool supportsStorageBuffers = false;
    int maxBindGroups = 4;
    int maxStorageBuffersPerShader = 8;
    long maxBufferSize = 1L * 1024 * 1024 * 1024;  // 1 GB
};

// Singleton renderer. Picks the best backend on init().
class WebGPURenderer {
public:
    static WebGPURenderer& get() {
        static WebGPURenderer instance;
        return instance;
    }

    // Initialize with the given backend (or Auto). Returns true on success.
    // If WebGPU is requested but not available, falls back to WebGL2 and
    // logs a warning.
    bool init(Backend preferred = Backend::Auto) {
        // Scaffolding: detect capabilities but always report Auto → WebGL2
        // since real WebGPU integration requires Dawn/wgpu-native or
        // emscripten_webgpu_* bindings (added in a future pass).
        caps_.supportsWebGPU = false;
        caps_.supportsComputeShaders = false;
        caps_.supportsStorageBuffers = false;
        caps_.maxBindGroups = 4;
        caps_.maxStorageBuffersPerShader = 8;
        caps_.maxBufferSize = 1L * 1024 * 1024 * 1024;
        active_ = Backend::WebGL2;
        initialized_ = true;
        (void)preferred;
        return true;
    }

    void shutdown() {
        initialized_ = false;
        device_.reset();
        swapChain_.reset();
    }

    // Capabilities — call after init().
    const DeviceCapabilities& caps() const { return caps_; }
    Backend activeBackend() const { return active_; }

    // Swap chain management. The canvas is identified by a string handle
    // (e.g. "#game-canvas" on web, an HWND on Windows).
    bool createSwapChain(const std::string& canvasHandle, int width, int height);
    void resizeSwapChain(int width, int height);

    // Buffer creation. Returns nullptr on failure.
    std::shared_ptr<Buffer> createBuffer(const void* data, size_t size,
                                         bool isUniform = false,
                                         bool isStorage = false);
    void updateBuffer(const std::shared_ptr<Buffer>& buf,
                      const void* data, size_t offset, size_t size);

    // Pipeline creation from WGSL source. Vertex + fragment shaders.
    std::shared_ptr<Pipeline> createPipeline(const std::string& wgslSource);

    // Draw. Encodes a draw command into the current frame's command buffer.
    void beginFrame();
    void bindPipeline(const std::shared_ptr<Pipeline>& pipeline);
    void bindUniformBuffer(int group, int binding,
                           const std::shared_ptr<Buffer>& buf);
    void setVertexData(const std::shared_ptr<Buffer>& buf);
    void setIndexData(const std::shared_ptr<Buffer>& buf);
    void draw(int vertexCount, int instanceCount = 1, int firstVertex = 0);
    void drawIndexed(int indexCount, int instanceCount = 1,
                     int firstIndex = 0, int vertexOffset = 0);
    void endFrame();

    // Compute shaders (WebGPU only — WebGL2 has no compute).
    std::shared_ptr<Pipeline> createComputePipeline(const std::string& wgslSource);
    void dispatchCompute(int x, int y, int z);

private:
    DeviceCapabilities caps_;
    Backend active_ = Backend::WebGL2;
    bool initialized_ = false;

    // In a real impl, these would be raw WGPUDevice / WGPUSwapChain / etc.
    // Here they're empty structs so the API compiles.
    std::shared_ptr<Device> device_;
    std::shared_ptr<SwapChain> swapChain_;
};

// ---------------------------------------------------------------------------
// WGSL shader templates — convenience for common shader patterns.
// ---------------------------------------------------------------------------
inline const char* kTriangleWGSL = R"WGSL(
@vertex
fn vs_main(@builtin(vertex_index) idx: u32) -> @builtin(position) vec4<f32> {
    var positions = array<vec2<f32>, 3>(
        vec2<f32>( 0.0,  0.5),
        vec2<f32>(-0.5, -0.5),
        vec2<f32>( 0.5, -0.5),
    );
    return vec4<f32>(positions[idx], 0.0, 1.0);
}

@fragment
fn fs_main() -> @location(0) vec4<f32> {
    return vec4<f32>(1.0, 0.4, 0.2, 1.0);
}
)WGSL";

inline const char* kTexturedQuadWGSL = R"WGSL(
struct Uniforms {
    mvp: mat4x4<f32>,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) texcoord: vec2<f32>,
};

struct VertexOutput {
    @builtin(position) clip_position: vec4<f32>,
    @location(0) texcoord: vec2<f32>,
};

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    out.clip_position = uniforms.mvp * vec4<f32>(in.position, 1.0);
    out.texcoord = in.texcoord;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    return textureSample(tex, texSampler, in.texcoord);
}
)WGSL";

inline const char* kComputeParticleWGSL = R"WGSL(
// Particle system compute shader. Each thread updates one particle.
// Reads current position + velocity from storage buffer, integrates
// position, applies gravity, writes back.
struct Particle {
    position: vec3<f32>,
    velocity: vec3<f32>,
    lifetime: f32,
    pad: f32,
};

@group(0) @binding(0) var<storage, read_write> particles: array<Particle>;

@compute @workgroup_size(64)
fn cs_main(@builtin(global_invocation_id) gid: vec3<u32>) {
    let idx = gid.x;
    if (idx >= arrayLength(&particles)) { return; }
    var p = particles[idx];
    p.velocity.y = p.velocity.y - 9.8 * 0.016;  // gravity, dt=16ms
    p.position = p.position + p.velocity * 0.016;
    p.lifetime = p.lifetime - 0.016;
    if (p.lifetime < 0.0) {
        p.position = vec3<f32>(0.0, 0.0, 0.0);
        p.velocity = vec3<f32>(0.0, 5.0, 0.0);
        p.lifetime = 3.0;
    }
    particles[idx] = p;
}
)WGSL";

} // namespace webgpu
} // namespace td
