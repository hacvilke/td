#include "asset_loader.h"
#include "png_decoder.h"
#include "obj_loader.h"
#include "../core/logger.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace td {

AssetLoader AssetLoader::s_instance;

AssetLoader& AssetLoader::get() {
    return s_instance;
}

AssetLoader::~AssetLoader() {
    shutdown();
}

bool AssetLoader::init() {
    memset(m_assets, 0, sizeof(m_assets));
    m_assetCount = 0;
    TD_LOG_INFO("Asset loader initialized");
    return true;
}

void AssetLoader::shutdown() {
    unloadAll();
    TD_LOG_INFO("Asset loader shutdown");
}

uint32_t AssetLoader::hashPath(const char* path) const {
    // FNV-1a hash
    uint32_t hash = 2166136261u;
    while (*path) {
        hash ^= (uint8_t)*path++;
        hash *= 16777619u;
    }
    return hash;
}

int AssetLoader::findAsset(const char* path) const {
    uint32_t hash = hashPath(path);
    
    for (int i = 0; i < TD_MAX_ASSETS; i++) {
        if (m_assets[i].loaded && m_assets[i].pathHash == hash) {
            if (strcmp(m_assets[i].path, path) == 0) {
                return i;
            }
        }
    }
    
    return -1;
}

int AssetLoader::findOrCreateSlot(const char* path, AssetType type) {
    // Check if already loaded
    int existing = findAsset(path);
    if (existing >= 0) {
        return existing;
    }
    
    // Find free slot
    for (int i = 0; i < TD_MAX_ASSETS; i++) {
        if (!m_assets[i].loaded) {
            strncpy(m_assets[i].path, path, 255);
            m_assets[i].path[255] = '\0';
            m_assets[i].pathHash = hashPath(path);
            m_assets[i].type = type;
            m_assets[i].loaded = false;
            m_assets[i].data = nullptr;
            return i;
        }
    }
    
    TD_LOG_ERROR("Asset slots exhausted");
    return -1;
}

AssetType AssetLoader::guessTypeFromPath(const char* path) const {
    const char* ext = strrchr(path, '.');
    if (!ext) return AssetType::Unknown;
    
    if (strcmp(ext, ".png") == 0 || strcmp(ext, ".PNG") == 0) return AssetType::Texture;
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".JPG") == 0) return AssetType::Texture;
    if (strcmp(ext, ".jpeg") == 0 || strcmp(ext, ".JPEG") == 0) return AssetType::Texture;
    if (strcmp(ext, ".bmp") == 0 || strcmp(ext, ".BMP") == 0) return AssetType::Texture;
    if (strcmp(ext, ".obj") == 0 || strcmp(ext, ".OBJ") == 0) return AssetType::Mesh;
    if (strcmp(ext, ".wav") == 0 || strcmp(ext, ".WAV") == 0) return AssetType::Sound;
    if (strcmp(ext, ".td") == 0 || strcmp(ext, ".TD") == 0) return AssetType::Script;
    if (strcmp(ext, ".vert") == 0 || strcmp(ext, ".frag") == 0) return AssetType::Shader;
    if (strcmp(ext, ".glsl") == 0 || strcmp(ext, ".GLSL") == 0) return AssetType::Shader;
    
    return AssetType::Unknown;
}

Texture* AssetLoader::loadTexture(const char* path) {
    // Check if already loaded
    int slot = findAsset(path);
    if (slot >= 0 && m_assets[slot].data) {
        return (Texture*)m_assets[slot].data;
    }
    
    // Create slot
    slot = findOrCreateSlot(path, AssetType::Texture);
    if (slot < 0) return nullptr;
    
    // Load PNG
    PNGDecoder decoder;
    PNGImage img;
    
    if (!decoder.decode(path, img)) {
        TD_LOG_ERROR("Failed to load texture: %s", path);
        return nullptr;
    }
    
    // Create texture
    Texture* texture = new Texture();
    TextureConfig config;
    config.width = img.width;
    config.height = img.height;
    config.channels = img.channels;
    
    if (!texture->create(config, img.pixels)) {
        decoder.free(img);
        delete texture;
        TD_LOG_ERROR("Failed to create texture from: %s", path);
        return nullptr;
    }
    
    decoder.free(img);
    
    m_assets[slot].data = texture;
    m_assets[slot].loaded = true;
    m_assetCount++;
    
    TD_LOG_INFO("Loaded texture: %s", path);
    return texture;
}

Mesh* AssetLoader::loadMesh(const char* path) {
    // Check if already loaded
    int slot = findAsset(path);
    if (slot >= 0 && m_assets[slot].data) {
        return (Mesh*)m_assets[slot].data;
    }
    
    // Create slot
    slot = findOrCreateSlot(path, AssetType::Mesh);
    if (slot < 0) return nullptr;
    
    // Load OBJ
    OBJLoader loader;
    Mesh* mesh = new Mesh();
    
    if (!loader.load(path, *mesh)) {
        delete mesh;
        TD_LOG_ERROR("Failed to load mesh: %s", path);
        return nullptr;
    }
    
    m_assets[slot].data = mesh;
    m_assets[slot].loaded = true;
    m_assetCount++;
    
    TD_LOG_INFO("Loaded mesh: %s", path);
    return mesh;
}

WAVData* AssetLoader::loadSound(const char* path) {
    // Check if already loaded
    int slot = findAsset(path);
    if (slot >= 0 && m_assets[slot].data) {
        return (WAVData*)m_assets[slot].data;
    }
    
    // Create slot
    slot = findOrCreateSlot(path, AssetType::Sound);
    if (slot < 0) return nullptr;
    
    // Load WAV
    WAVLoader loader;
    WAVData* wav = new WAVData();
    
    if (!loader.load(path, *wav)) {
        delete wav;
        TD_LOG_ERROR("Failed to load sound: %s", path);
        return nullptr;
    }
    
    m_assets[slot].data = wav;
    m_assets[slot].loaded = true;
    m_assetCount++;
    
    TD_LOG_INFO("Loaded sound: %s", path);
    return wav;
}

char* AssetLoader::loadTextFile(const char* path, int* outSize) {
    FILE* file = fopen(path, "rb");
    if (!file) {
        TD_LOG_ERROR("Failed to open file: %s", path);
        return nullptr;
    }
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char* data = (char*)malloc(size + 1);
    if (!data) {
        fclose(file);
        TD_LOG_ERROR("Failed to allocate memory for file: %s", path);
        return nullptr;
    }
    
    size_t bytesRead = fread(data, 1, size, file);
    data[bytesRead] = '\0';
    fclose(file);
    
    if (outSize) {
        *outSize = (int)bytesRead;
    }
    
    return data;
}

void AssetLoader::unloadTexture(const char* path) {
    int slot = findAsset(path);
    if (slot < 0) return;
    
    if (m_assets[slot].type == AssetType::Texture && m_assets[slot].data) {
        delete (Texture*)m_assets[slot].data;
        m_assets[slot].data = nullptr;
        m_assets[slot].loaded = false;
        m_assetCount--;
    }
}

void AssetLoader::unloadMesh(const char* path) {
    int slot = findAsset(path);
    if (slot < 0) return;
    
    if (m_assets[slot].type == AssetType::Mesh && m_assets[slot].data) {
        delete (Mesh*)m_assets[slot].data;
        m_assets[slot].data = nullptr;
        m_assets[slot].loaded = false;
        m_assetCount--;
    }
}

void AssetLoader::unloadSound(const char* path) {
    int slot = findAsset(path);
    if (slot < 0) return;
    
    if (m_assets[slot].type == AssetType::Sound && m_assets[slot].data) {
        WAVLoader loader;
        loader.free(*(WAVData*)m_assets[slot].data);
        delete (WAVData*)m_assets[slot].data;
        m_assets[slot].data = nullptr;
        m_assets[slot].loaded = false;
        m_assetCount--;
    }
}

void AssetLoader::unloadAll() {
    for (int i = 0; i < TD_MAX_ASSETS; i++) {
        if (m_assets[i].loaded && m_assets[i].data) {
            switch (m_assets[i].type) {
                case AssetType::Texture:
                    delete (Texture*)m_assets[i].data;
                    break;
                case AssetType::Mesh:
                    delete (Mesh*)m_assets[i].data;
                    break;
                case AssetType::Sound: {
                    WAVLoader loader;
                    loader.free(*(WAVData*)m_assets[i].data);
                    delete (WAVData*)m_assets[i].data;
                    break;
                }
                default:
                    break;
            }
            m_assets[i].data = nullptr;
            m_assets[i].loaded = false;
        }
    }
    m_assetCount = 0;
}

bool AssetLoader::isLoaded(const char* path) const {
    int slot = findAsset(path);
    return slot >= 0 && m_assets[slot].loaded;
}

AssetType AssetLoader::getAssetType(const char* path) const {
    int slot = findAsset(path);
    if (slot >= 0) {
        return m_assets[slot].type;
    }
    return guessTypeFromPath(path);
}

} // namespace td
