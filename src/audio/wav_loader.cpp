#include "wav_loader.h"
#include "../core/logger.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace td {

int WAVData::getSampleCount() const {
    if (bitsPerSample == 0 || numChannels == 0) return 0;
    return dataSize / (numChannels * bitsPerSample / 8);
}

bool WAVLoader::readChunkHeader(void* file, char* outId, uint32_t* outSize) {
    FILE* f = (FILE*)file;
    if (fread(outId, 1, 4, f) != 4) return false;
    if (fread(outSize, 4, 1, f) != 1) return false;
    return true;
}

bool WAVLoader::load(const char* path, WAVData& out) {
    memset(&out, 0, sizeof(WAVData));
    
    FILE* file = fopen(path, "rb");
    if (!file) {
        snprintf(m_errorMsg, sizeof(m_errorMsg), "Failed to open file: %s", path);
        return false;
    }
    
    // Read RIFF header
    char riffId[4];
    uint32_t fileSize;
    char waveId[4];
    
    if (fread(riffId, 1, 4, file) != 4 || memcmp(riffId, "RIFF", 4) != 0) {
        snprintf(m_errorMsg, sizeof(m_errorMsg), "Invalid RIFF header");
        fclose(file);
        return false;
    }
    
    if (fread(&fileSize, 4, 1, file) != 1) {
        snprintf(m_errorMsg, sizeof(m_errorMsg), "Failed to read file size");
        fclose(file);
        return false;
    }
    
    if (fread(waveId, 1, 4, file) != 4 || memcmp(waveId, "WAVE", 4) != 0) {
        snprintf(m_errorMsg, sizeof(m_errorMsg), "Invalid WAVE header");
        fclose(file);
        return false;
    }
    
    bool foundFmt = false;
    bool foundData = false;
    
    // Read chunks
    while (!foundData) {
        char chunkId[4];
        uint32_t chunkSize;
        
        if (!readChunkHeader(file, chunkId, &chunkSize)) {
            break;
        }
        
        if (memcmp(chunkId, "fmt ", 4) == 0) {
            // Format chunk
            uint16_t audioFormat;
            if (fread(&audioFormat, 2, 1, file) != 1) {
                snprintf(m_errorMsg, sizeof(m_errorMsg), "Failed to read audio format");
                fclose(file);
                return false;
            }
            
            if (audioFormat != 1) { // 1 = PCM
                snprintf(m_errorMsg, sizeof(m_errorMsg), 
                         "Unsupported audio format: %d (only PCM supported)", audioFormat);
                fclose(file);
                return false;
            }
            
            if (fread(&out.numChannels, 2, 1, file) != 1 ||
                fread(&out.sampleRate, 4, 1, file) != 1) {
                snprintf(m_errorMsg, sizeof(m_errorMsg), "Failed to read format data");
                fclose(file);
                return false;
            }
            
            uint32_t byteRate;
            uint16_t blockAlign;
            if (fread(&byteRate, 4, 1, file) != 1 ||
                fread(&blockAlign, 2, 1, file) != 1 ||
                fread(&out.bitsPerSample, 2, 1, file) != 1) {
                snprintf(m_errorMsg, sizeof(m_errorMsg), "Failed to read format data");
                fclose(file);
                return false;
            }
            
            // Skip any extra format bytes
            if (chunkSize > 16) {
                fseek(file, chunkSize - 16, SEEK_CUR);
            }
            
            foundFmt = true;
        }
        else if (memcmp(chunkId, "data", 4) == 0) {
            // Data chunk
            if (!foundFmt) {
                snprintf(m_errorMsg, sizeof(m_errorMsg), 
                         "Data chunk found before format chunk");
                fclose(file);
                return false;
            }
            
            out.dataSize = chunkSize;
            out.data = (uint8_t*)malloc(chunkSize);
            
            if (!out.data) {
                snprintf(m_errorMsg, sizeof(m_errorMsg), "Failed to allocate audio data");
                fclose(file);
                return false;
            }
            
            size_t bytesRead = fread(out.data, 1, chunkSize, file);
            if (bytesRead != chunkSize) {
                snprintf(m_errorMsg, sizeof(m_errorMsg), 
                         "Failed to read audio data (got %zu, expected %u)", 
                         bytesRead, chunkSize);
                ::free(out.data);
                out.data = nullptr;
                fclose(file);
                return false;
            }
            
            foundData = true;
        }
        else {
            // Skip unknown chunk (handle padding for odd sizes)
            uint32_t skipSize = chunkSize;
            if (chunkSize & 1) skipSize++; // Word alignment padding
            fseek(file, skipSize, SEEK_CUR);
        }
    }
    
    fclose(file);
    
    if (!foundFmt || !foundData) {
        snprintf(m_errorMsg, sizeof(m_errorMsg), "Missing required chunks");
        if (out.data) {
            ::free(out.data);
            out.data = nullptr;
        }
        return false;
    }
    
    // Calculate duration
    out.numSamples = out.getSampleCount();
    out.durationSeconds = (float)out.numSamples / (float)out.sampleRate;
    
    TD_LOG_INFO("Loaded WAV: %s (%u Hz, %d ch, %d bit, %.2f sec)",
                path, out.sampleRate, out.numChannels, 
                out.bitsPerSample, out.durationSeconds);
    
    return true;
}

void WAVLoader::free(WAVData& wav) {
    if (wav.data) {
        ::free(wav.data);
    }
    memset(&wav, 0, sizeof(WAVData));
}

} // namespace td
