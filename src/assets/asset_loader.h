#pragma once
#include "../renderer/texture.h"
#include "../renderer/mesh.h"
#include "../audio/wav_loader.h"
#include <cstdint>

namespace td {

#define TD_MAX_ASSETS 256

enum class AssetType : uint8_t {
    Unknown = 0,
    Texture,
    Mesh,
    Sound,
    Script,
    Shader
};

struct AssetEntry {
    char path[256] = {};
    uint32_t pathHash = 0;
    AssetType type = AssetType::Unknown;
    void* data = nullptr;
    bool loaded = false;
};

class AssetLoader {
public:
    static AssetLoader& get();
    
    bool init();
    void shutdown();
    
    // Load assets
    Texture* loadTexture(const char* path);
    Mesh* loadMesh(const char* path);
    WAVData* loadSound(const char* path);
    char* loadTextFile(const char* path, int* outSize = nullptr);
    
    // Unload
    void unloadTexture(const char* path);
    void unloadMesh(const char* path);
    void unloadSound(const char* path);
    void unloadAll();
    
    // Check if loaded
    bool isLoaded(const char* path) const;
    AssetType getAssetType(const char* path) const;
    
    // Get stats
    int getLoadedCount() const { return m_assetCount; }
    
private:
    AssetLoader() = default;
    ~AssetLoader();
    
    uint32_t hashPath(const char* path) const;
    int findAsset(const char* path) const;
    int findOrCreateSlot(const char* path, AssetType type);
    AssetType guessTypeFromPath(const char* path) const;
    
    AssetEntry m_assets[TD_MAX_ASSETS];
    int m_assetCount = 0;
    
    static AssetLoader s_instance;
};

} // namespace td
