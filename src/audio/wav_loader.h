#pragma once
#include <cstdint>

namespace td {

struct WAVData {
    uint16_t numChannels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    uint32_t dataSize = 0;
    uint8_t* data = nullptr;
    float durationSeconds = 0;
    uint32_t numSamples = 0;
    
    int getSampleCount() const;
    int getBytesPerSample() const { return bitsPerSample / 8; }
    int getBlockAlign() const { return numChannels * getBytesPerSample(); }
};

class WAVLoader {
public:
    bool load(const char* path, WAVData& out);
    void free(WAVData& wav);
    
    const char* getLastError() const { return m_errorMsg; }
    
private:
    bool readChunkHeader(void* file, char* outId, uint32_t* outSize);
    
    char m_errorMsg[256] = {};
};

} // namespace td
