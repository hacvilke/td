#pragma once
#include <cstdint>

namespace td {

struct PNGImage {
    int width = 0;
    int height = 0;
    int channels = 4;  // Always output RGBA
    uint8_t* pixels = nullptr;
};

class PNGDecoder {
public:
    bool decode(const char* path, PNGImage& out);
    void free(PNGImage& img);
    
    const char* getError() const { return m_error; }
    
private:
    // PNG chunk types
    static constexpr uint32_t CHUNK_IHDR = 0x49484452;
    static constexpr uint32_t CHUNK_PLTE = 0x504C5445;
    static constexpr uint32_t CHUNK_IDAT = 0x49444154;
    static constexpr uint32_t CHUNK_IEND = 0x49454E44;
    
    // Zlib/Deflate decompression
    bool inflate(const uint8_t* compressed, uint32_t compressedSize,
                 uint8_t** outData, uint32_t* outSize);
    
    // PNG filters
    bool unfilter(uint8_t* data, int width, int height, int channels);
    uint8_t paethPredictor(uint8_t a, uint8_t b, uint8_t c);
    
    // Helpers
    uint32_t readBE32(const uint8_t* data);
    bool verifySignature(const uint8_t* data);
    
    char m_error[256] = {};
    
    // Palette for indexed color
    uint8_t m_palette[256 * 3];
    int m_paletteSize = 0;
};

} // namespace td
