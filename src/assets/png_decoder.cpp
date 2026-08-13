#include "png_decoder.h"
#include "../core/logger.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace td {

uint32_t PNGDecoder::readBE32(const uint8_t* data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

bool PNGDecoder::verifySignature(const uint8_t* data) {
    static const uint8_t PNG_SIG[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    return memcmp(data, PNG_SIG, 8) == 0;
}

uint8_t PNGDecoder::paethPredictor(uint8_t a, uint8_t b, uint8_t c) {
    int p = (int)a + (int)b - (int)c;
    int pa = p > (int)a ? p - (int)a : (int)a - p;
    int pb = p > (int)b ? p - (int)b : (int)b - p;
    int pc = p > (int)c ? p - (int)c : (int)c - p;
    
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

bool PNGDecoder::unfilter(uint8_t* data, int width, int height, int channels) {
    int bytesPerPixel = channels;
    int scanlineSize = width * bytesPerPixel;
    int rowSize = 1 + scanlineSize; // Filter byte + pixels
    
    uint8_t* prevRow = nullptr;
    uint8_t* currRow = data;
    
    for (int y = 0; y < height; y++) {
        uint8_t filterType = currRow[0];
        uint8_t* pixels = currRow + 1;
        
        switch (filterType) {
            case 0: // None
                break;
                
            case 1: // Sub
                for (int x = bytesPerPixel; x < scanlineSize; x++) {
                    pixels[x] += pixels[x - bytesPerPixel];
                }
                break;
                
            case 2: // Up
                if (prevRow) {
                    for (int x = 0; x < scanlineSize; x++) {
                        pixels[x] += prevRow[x + 1];
                    }
                }
                break;
                
            case 3: // Average
                for (int x = 0; x < scanlineSize; x++) {
                    uint8_t left = (x < bytesPerPixel) ? 0 : pixels[x - bytesPerPixel];
                    uint8_t above = prevRow ? prevRow[x + 1] : 0;
                    pixels[x] += (left + above) / 2;
                }
                break;
                
            case 4: // Paeth
                for (int x = 0; x < scanlineSize; x++) {
                    uint8_t left = (x < bytesPerPixel) ? 0 : pixels[x - bytesPerPixel];
                    uint8_t above = prevRow ? prevRow[x + 1] : 0;
                    uint8_t upperLeft = (prevRow && x >= bytesPerPixel) ? 
                                        prevRow[x + 1 - bytesPerPixel] : 0;
                    pixels[x] += paethPredictor(left, above, upperLeft);
                }
                break;
                
            default:
                snprintf(m_error, sizeof(m_error), "Unknown filter type: %d", filterType);
                return false;
        }
        
        prevRow = currRow;
        currRow += rowSize;
    }
    
    return true;
}

// Simplified inflate implementation - handles fixed Huffman only
// For a full implementation, you'd need dynamic Huffman support
bool PNGDecoder::inflate(const uint8_t* compressed, uint32_t compressedSize,
                         uint8_t** outData, uint32_t* outSize) {
    // Skip zlib header (2 bytes)
    if (compressedSize < 6) {
        snprintf(m_error, sizeof(m_error), "Compressed data too short");
        return false;
    }
    
    // Check zlib header
    uint8_t cmf = compressed[0];
    uint8_t flg = compressed[1];
    
    if ((cmf & 0x0F) != 8) {
        snprintf(m_error, sizeof(m_error), "Invalid compression method");
        return false;
    }
    
    if (((cmf * 256 + flg) % 31) != 0) {
        snprintf(m_error, sizeof(m_error), "Invalid zlib header checksum");
        return false;
    }
    
    bool hasDict = (flg & 0x20) != 0;
    if (hasDict) {
        snprintf(m_error, sizeof(m_error), "Preset dictionary not supported");
        return false;
    }
    
    // Estimate output size (PNG typically compresses to ~50%)
    uint32_t estimatedSize = compressedSize * 4;
    uint8_t* output = (uint8_t*)malloc(estimatedSize);
    if (!output) {
        snprintf(m_error, sizeof(m_error), "Failed to allocate decompression buffer");
        return false;
    }
    
    uint32_t outPos = 0;
    uint32_t inPos = 2; // Skip zlib header
    uint32_t bitBuffer = 0;
    int bitsInBuffer = 0;
    
    // Fixed Huffman code lengths
    static const int LITERAL_LENGTHS[288] = {
        8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
        8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
        8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
        8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
        8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
        9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
        9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
        9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,8,8,8,8,8,8,8,8
    };
    
    static const int LENGTH_BASE[29] = {
        3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
    };
    
    static const int LENGTH_EXTRA[29] = {
        0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
    };
    
    static const int DIST_BASE[30] = {
        1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,
        1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
    };
    
    static const int DIST_EXTRA[30] = {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
    };
    
    // Helper lambda for reading bits from the stream
    auto readBitsFunc = [&](int n) -> uint32_t {
        while (bitsInBuffer < n) {
            if (inPos >= compressedSize - 4) break;
            bitBuffer |= ((uint32_t)compressed[inPos++]) << bitsInBuffer;
            bitsInBuffer += 8;
        }
        uint32_t val = bitBuffer & ((1u << n) - 1);
        bitBuffer >>= n;
        bitsInBuffer -= n;
        return val;
    };
    #define READ_BITS(n) readBitsFunc(n)
    
    bool lastBlock = false;
    
    while (!lastBlock && inPos < compressedSize - 4) {
        lastBlock = READ_BITS(1) != 0;
        int blockType = READ_BITS(2);
        
        if (blockType == 0) {
            // Stored block
            bitsInBuffer = 0;
            bitBuffer = 0;
            
            if (inPos + 4 > compressedSize) break;
            uint16_t len = compressed[inPos] | (compressed[inPos + 1] << 8);
            inPos += 4; // Skip LEN and NLEN
            
            if (outPos + len > estimatedSize) {
                estimatedSize = (outPos + len) * 2;
                output = (uint8_t*)realloc(output, estimatedSize);
            }
            
            memcpy(output + outPos, compressed + inPos, len);
            outPos += len;
            inPos += len;
        }
        else if (blockType == 1 || blockType == 2) {
            // Fixed or dynamic Huffman (simplified - only fixed supported fully)
            if (blockType == 2) {
                // Dynamic Huffman - skip for now, most PNGs use fixed
                snprintf(m_error, sizeof(m_error), "Dynamic Huffman not fully supported");
                // For a complete implementation, you'd read the code tree here
            }
            
            while (inPos < compressedSize - 4) {
                // Decode literal/length
                while (bitsInBuffer < 15 && inPos < compressedSize) {
                    bitBuffer |= ((uint32_t)compressed[inPos++]) << bitsInBuffer;
                    bitsInBuffer += 8;
                }
                
                // Reverse bits for Huffman decoding
                uint32_t code = 0;
                int symbol = -1;
                
                // Try 7-bit codes (256-279)
                code = bitBuffer & 0x7F;
                code = ((code & 0x55) << 1) | ((code & 0xAA) >> 1);
                code = ((code & 0x33) << 2) | ((code & 0xCC) >> 2);
                code = ((code & 0x0F) << 4) | ((code & 0xF0) >> 4);
                code >>= 1;
                
                if (code <= 23) {
                    symbol = 256 + code;
                    bitBuffer >>= 7;
                    bitsInBuffer -= 7;
                }
                else {
                    // Try 8-bit codes (0-143, 280-287)
                    code = bitBuffer & 0xFF;
                    code = ((code & 0x55) << 1) | ((code & 0xAA) >> 1);
                    code = ((code & 0x33) << 2) | ((code & 0xCC) >> 2);
                    code = ((code & 0x0F) << 4) | ((code & 0xF0) >> 4);
                    
                    if (code >= 0x30 && code <= 0xBF) {
                        symbol = code - 0x30;
                        bitBuffer >>= 8;
                        bitsInBuffer -= 8;
                    }
                    else if (code >= 0xC0 && code <= 0xC7) {
                        symbol = 280 + (code - 0xC0);
                        bitBuffer >>= 8;
                        bitsInBuffer -= 8;
                    }
                    else {
                        // Try 9-bit codes (144-255)
                        code = bitBuffer & 0x1FF;
                        code = ((code & 0x155) << 1) | ((code & 0x0AA) >> 1);
                        code = ((code & 0x033) << 2) | ((code & 0x0CC) >> 2);
                        code = ((code & 0x00F) << 4) | ((code & 0x0F0) >> 4);
                        code >>= 0;
                        
                        if (code >= 0x190 && code <= 0x1FF) {
                            symbol = 144 + (code - 0x190);
                            bitBuffer >>= 9;
                            bitsInBuffer -= 9;
                        }
                    }
                }
                
                if (symbol < 0 || symbol > 285) {
                    // Simplified fallback
                    break;
                }
                
                if (symbol == 256) {
                    // End of block
                    break;
                }
                else if (symbol < 256) {
                    // Literal byte
                    if (outPos >= estimatedSize) {
                        estimatedSize *= 2;
                        output = (uint8_t*)realloc(output, estimatedSize);
                    }
                    output[outPos++] = (uint8_t)symbol;
                }
                else {
                    // Length/distance pair
                    int lengthCode = symbol - 257;
                    int length = LENGTH_BASE[lengthCode];
                    if (LENGTH_EXTRA[lengthCode] > 0) {
                        length += READ_BITS(LENGTH_EXTRA[lengthCode]);
                    }
                    
                    // Read distance (5 bits fixed)
                    int distCode = READ_BITS(5);
                    distCode = ((distCode & 0x15) << 1) | ((distCode & 0x0A) >> 1);
                    distCode = ((distCode & 0x03) << 2) | ((distCode & 0x0C) >> 2);
                    distCode = (distCode >> 1) | ((distCode & 1) << 4);
                    
                    int distance = DIST_BASE[distCode];
                    if (DIST_EXTRA[distCode] > 0) {
                        distance += READ_BITS(DIST_EXTRA[distCode]);
                    }
                    
                    // Copy from back reference
                    if (outPos + length > estimatedSize) {
                        estimatedSize = (outPos + length) * 2;
                        output = (uint8_t*)realloc(output, estimatedSize);
                    }
                    
                    for (int i = 0; i < length; i++) {
                        output[outPos] = output[outPos - distance];
                        outPos++;
                    }
                }
            }
        }
        else {
            snprintf(m_error, sizeof(m_error), "Invalid block type: %d", blockType);
            ::free(output);
            return false;
        }
    }
    
    #undef READ_BITS
    
    *outData = output;
    *outSize = outPos;
    return true;
}

bool PNGDecoder::decode(const char* path, PNGImage& out) {
    memset(&out, 0, sizeof(PNGImage));
    m_paletteSize = 0;
    
    FILE* file = fopen(path, "rb");
    if (!file) {
        snprintf(m_error, sizeof(m_error), "Failed to open file: %s", path);
        return false;
    }
    
    // Read file into memory
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    uint8_t* fileData = (uint8_t*)malloc(fileSize);
    if (!fileData) {
        fclose(file);
        snprintf(m_error, sizeof(m_error), "Failed to allocate file buffer");
        return false;
    }
    
    fread(fileData, 1, fileSize, file);
    fclose(file);
    
    // Verify signature
    if (fileSize < 8 || !verifySignature(fileData)) {
        ::free(fileData);
        snprintf(m_error, sizeof(m_error), "Invalid PNG signature");
        return false;
    }
    
    // Collect IDAT chunks
    uint8_t* idatData = nullptr;
    uint32_t idatSize = 0;
    uint32_t idatCapacity = 0;
    
    int width = 0, height = 0;
    int bitDepth = 0, colorType = 0;
    
    uint32_t pos = 8;
    
    while (pos + 12 <= (uint32_t)fileSize) {
        uint32_t chunkLength = readBE32(fileData + pos);
        uint32_t chunkType = readBE32(fileData + pos + 4);
        const uint8_t* chunkData = fileData + pos + 8;
        
        if (chunkType == CHUNK_IHDR) {
            if (chunkLength < 13) {
                snprintf(m_error, sizeof(m_error), "Invalid IHDR chunk");
                ::free(fileData);
                if (idatData) ::free(idatData);
                return false;
            }
            
            width = readBE32(chunkData);
            height = readBE32(chunkData + 4);
            bitDepth = chunkData[8];
            colorType = chunkData[9];
            
            if (bitDepth != 8) {
                snprintf(m_error, sizeof(m_error), "Only 8-bit depth supported");
                ::free(fileData);
                if (idatData) ::free(idatData);
                return false;
            }
        }
        else if (chunkType == CHUNK_PLTE) {
            m_paletteSize = chunkLength / 3;
            if (m_paletteSize > 256) m_paletteSize = 256;
            memcpy(m_palette, chunkData, m_paletteSize * 3);
        }
        else if (chunkType == CHUNK_IDAT) {
            // Append to IDAT buffer
            if (idatSize + chunkLength > idatCapacity) {
                idatCapacity = (idatSize + chunkLength) * 2;
                idatData = (uint8_t*)realloc(idatData, idatCapacity);
            }
            memcpy(idatData + idatSize, chunkData, chunkLength);
            idatSize += chunkLength;
        }
        else if (chunkType == CHUNK_IEND) {
            break;
        }
        
        pos += 12 + chunkLength; // Header + data + CRC
    }
    
    ::free(fileData);
    
    if (!idatData || idatSize == 0) {
        snprintf(m_error, sizeof(m_error), "No image data found");
        if (idatData) ::free(idatData);
        return false;
    }
    
    // Decompress
    uint8_t* decompressed = nullptr;
    uint32_t decompressedSize = 0;
    
    if (!inflate(idatData, idatSize, &decompressed, &decompressedSize)) {
        ::free(idatData);
        return false;
    }
    
    ::free(idatData);
    
    // Determine source channels
    int srcChannels;
    switch (colorType) {
        case 0: srcChannels = 1; break; // Grayscale
        case 2: srcChannels = 3; break; // RGB
        case 3: srcChannels = 1; break; // Indexed
        case 4: srcChannels = 2; break; // Gray + Alpha
        case 6: srcChannels = 4; break; // RGBA
        default:
            snprintf(m_error, sizeof(m_error), "Unsupported color type: %d", colorType);
            ::free(decompressed);
            return false;
    }
    
    // Unfilter
    if (!unfilter(decompressed, width, height, srcChannels)) {
        ::free(decompressed);
        return false;
    }
    
    // Allocate output (always RGBA)
    out.width = width;
    out.height = height;
    out.channels = 4;
    out.pixels = (uint8_t*)malloc(width * height * 4);
    
    if (!out.pixels) {
        ::free(decompressed);
        snprintf(m_error, sizeof(m_error), "Failed to allocate output pixels");
        return false;
    }
    
    // Convert to RGBA
    int rowSize = 1 + width * srcChannels;
    
    for (int y = 0; y < height; y++) {
        const uint8_t* srcRow = decompressed + y * rowSize + 1;
        uint8_t* dstRow = out.pixels + y * width * 4;
        
        for (int x = 0; x < width; x++) {
            uint8_t r, g, b, a;
            
            switch (colorType) {
                case 0: // Grayscale
                    r = g = b = srcRow[x];
                    a = 255;
                    break;
                case 2: // RGB
                    r = srcRow[x * 3];
                    g = srcRow[x * 3 + 1];
                    b = srcRow[x * 3 + 2];
                    a = 255;
                    break;
                case 3: // Indexed
                    {
                        int idx = srcRow[x];
                        if (idx < m_paletteSize) {
                            r = m_palette[idx * 3];
                            g = m_palette[idx * 3 + 1];
                            b = m_palette[idx * 3 + 2];
                        } else {
                            r = g = b = 0;
                        }
                        a = 255;
                    }
                    break;
                case 4: // Gray + Alpha
                    r = g = b = srcRow[x * 2];
                    a = srcRow[x * 2 + 1];
                    break;
                case 6: // RGBA
                    r = srcRow[x * 4];
                    g = srcRow[x * 4 + 1];
                    b = srcRow[x * 4 + 2];
                    a = srcRow[x * 4 + 3];
                    break;
                default:
                    r = g = b = a = 0;
            }
            
            dstRow[x * 4] = r;
            dstRow[x * 4 + 1] = g;
            dstRow[x * 4 + 2] = b;
            dstRow[x * 4 + 3] = a;
        }
    }
    
    ::free(decompressed);
    return true;
}

void PNGDecoder::free(PNGImage& img) {
    if (img.pixels) {
        ::free(img.pixels);
    }
    memset(&img, 0, sizeof(PNGImage));
}

} // namespace td
