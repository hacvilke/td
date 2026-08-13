#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace td {

// Arena (Linear) Allocator
// Fast bump pointer allocation, reset all at once
class ArenaAllocator {
public:
    ArenaAllocator() : m_memory(nullptr), m_size(0), m_offset(0), m_peak(0) {}
    
    ~ArenaAllocator() {
        shutdown();
    }
    
    bool init(size_t sizeBytes) {
        m_memory = (uint8_t*)malloc(sizeBytes);
        if (!m_memory) {
            return false;
        }
        m_size = sizeBytes;
        m_offset = 0;
        m_peak = 0;
        return true;
    }
    
    void shutdown() {
        if (m_memory) {
            free(m_memory);
            m_memory = nullptr;
        }
        m_size = 0;
        m_offset = 0;
        m_peak = 0;
    }
    
    void* allocate(size_t sizeBytes, size_t alignment = 8) {
        // Align the current offset
        size_t alignedOffset = (m_offset + alignment - 1) & ~(alignment - 1);
        
        if (alignedOffset + sizeBytes > m_size) {
            // Out of memory
            return nullptr;
        }
        
        void* ptr = m_memory + alignedOffset;
        m_offset = alignedOffset + sizeBytes;
        
        if (m_offset > m_peak) {
            m_peak = m_offset;
        }
        
        return ptr;
    }
    
    template<typename T>
    T* allocate(size_t count = 1) {
        return (T*)allocate(sizeof(T) * count, alignof(T));
    }
    
    template<typename T, typename... Args>
    T* create(Args&&... args) {
        T* ptr = allocate<T>();
        if (ptr) {
            new (ptr) T(static_cast<Args&&>(args)...);
        }
        return ptr;
    }
    
    void reset() {
        m_offset = 0;
    }
    
    void clear() {
        m_offset = 0;
        memset(m_memory, 0, m_size);
    }
    
    size_t getUsed() const { return m_offset; }
    size_t getSize() const { return m_size; }
    size_t getPeak() const { return m_peak; }
    size_t getRemaining() const { return m_size - m_offset; }
    
    // Get marker for temporary allocations
    size_t getMarker() const { return m_offset; }
    void restoreToMarker(size_t marker) { m_offset = marker; }
    
private:
    uint8_t* m_memory;
    size_t m_size;
    size_t m_offset;
    size_t m_peak;
};

// Pool Allocator
// Fixed-size block allocation with free list
class PoolAllocator {
public:
    PoolAllocator() : m_memory(nullptr), m_freeList(nullptr), 
                      m_blockSize(0), m_blockCount(0), m_usedCount(0) {}
    
    ~PoolAllocator() {
        shutdown();
    }
    
    bool init(size_t blockSize, size_t blockCount) {
        // Ensure block size is at least pointer size for free list
        if (blockSize < sizeof(void*)) {
            blockSize = sizeof(void*);
        }
        
        // Align block size to 8 bytes
        blockSize = (blockSize + 7) & ~7;
        
        m_blockSize = blockSize;
        m_blockCount = blockCount;
        
        m_memory = (uint8_t*)malloc(blockSize * blockCount);
        if (!m_memory) {
            return false;
        }
        
        // Build free list
        m_freeList = m_memory;
        uint8_t* current = m_memory;
        
        for (size_t i = 0; i < blockCount - 1; i++) {
            uint8_t* next = current + blockSize;
            *((void**)current) = next;
            current = next;
        }
        
        // Last block points to null
        *((void**)current) = nullptr;
        
        m_usedCount = 0;
        return true;
    }
    
    void shutdown() {
        if (m_memory) {
            free(m_memory);
            m_memory = nullptr;
        }
        m_freeList = nullptr;
        m_blockSize = 0;
        m_blockCount = 0;
        m_usedCount = 0;
    }
    
    void* allocate() {
        if (!m_freeList) {
            // Pool exhausted
            return nullptr;
        }
        
        void* block = m_freeList;
        m_freeList = *((uint8_t**)m_freeList);
        m_usedCount++;
        
        return block;
    }
    
    void free(void* ptr) {
        if (!ptr) return;
        
        // Verify pointer is within our pool
        if (ptr < m_memory || ptr >= m_memory + m_blockSize * m_blockCount) {
            return; // Invalid pointer
        }
        
        // Add to free list
        *((void**)ptr) = m_freeList;
        m_freeList = (uint8_t*)ptr;
        m_usedCount--;
    }
    
    template<typename T>
    T* allocate() {
        return (T*)allocate();
    }
    
    template<typename T, typename... Args>
    T* create(Args&&... args) {
        T* ptr = allocate<T>();
        if (ptr) {
            new (ptr) T(static_cast<Args&&>(args)...);
        }
        return ptr;
    }
    
    template<typename T>
    void destroy(T* ptr) {
        if (ptr) {
            ptr->~T();
            free(ptr);
        }
    }
    
    size_t getBlockSize() const { return m_blockSize; }
    size_t getBlockCount() const { return m_blockCount; }
    size_t getUsedCount() const { return m_usedCount; }
    size_t getFreeCount() const { return m_blockCount - m_usedCount; }
    
private:
    uint8_t* m_memory;
    uint8_t* m_freeList;
    size_t m_blockSize;
    size_t m_blockCount;
    size_t m_usedCount;
};

// Stack Allocator
// LIFO allocation with headers for tracking
class StackAllocator {
public:
    StackAllocator() : m_memory(nullptr), m_size(0), m_offset(0) {}
    
    ~StackAllocator() {
        shutdown();
    }
    
    bool init(size_t sizeBytes) {
        m_memory = (uint8_t*)malloc(sizeBytes);
        if (!m_memory) {
            return false;
        }
        m_size = sizeBytes;
        m_offset = 0;
        return true;
    }
    
    void shutdown() {
        if (m_memory) {
            free(m_memory);
            m_memory = nullptr;
        }
        m_size = 0;
        m_offset = 0;
    }
    
    struct AllocationHeader {
        size_t previousOffset;
        size_t padding;
    };
    
    void* allocate(size_t sizeBytes, size_t alignment = 8) {
        size_t headerSize = sizeof(AllocationHeader);
        
        // Calculate aligned address after header
        size_t currentAddr = (size_t)(m_memory + m_offset + headerSize);
        size_t alignedAddr = (currentAddr + alignment - 1) & ~(alignment - 1);
        size_t padding = alignedAddr - currentAddr;
        
        size_t totalSize = headerSize + padding + sizeBytes;
        
        if (m_offset + totalSize > m_size) {
            return nullptr;
        }
        
        // Store header just before aligned address
        AllocationHeader* header = (AllocationHeader*)(m_memory + m_offset);
        header->previousOffset = m_offset;
        header->padding = padding + headerSize;
        
        void* ptr = (void*)(m_memory + m_offset + header->padding);
        m_offset += totalSize;
        
        return ptr;
    }
    
    void free(void* ptr) {
        if (!ptr) return;
        
        // Get header
        AllocationHeader* header = (AllocationHeader*)((uint8_t*)ptr - sizeof(AllocationHeader));
        
        // Verify this is the most recent allocation
        size_t ptrOffset = (uint8_t*)ptr - m_memory - header->padding;
        if (ptrOffset != header->previousOffset) {
            // Not LIFO order - this is a usage error
            return;
        }
        
        m_offset = header->previousOffset;
    }
    
    void reset() {
        m_offset = 0;
    }
    
    size_t getUsed() const { return m_offset; }
    size_t getSize() const { return m_size; }
    
private:
    uint8_t* m_memory;
    size_t m_size;
    size_t m_offset;
};

// Allocation macros
#define TD_ARENA_ALLOC(arena, type, count) \
    ((type*)(arena).allocate(sizeof(type) * (count), alignof(type)))

#define TD_POOL_ALLOC(pool, type) \
    ((type*)(pool).allocate())

#define TD_POOL_FREE(pool, ptr) \
    (pool).free(ptr)

// Global memory system
class MemorySystem {
public:
    static MemorySystem& get() {
        static MemorySystem instance;
        return instance;
    }
    
    bool init(size_t arenaSize = 64 * 1024 * 1024) { // 64 MB default
        return m_frameArena.init(arenaSize);
    }
    
    void shutdown() {
        m_frameArena.shutdown();
    }
    
    // Frame arena is reset every frame - use for temporary allocations
    ArenaAllocator& getFrameArena() { return m_frameArena; }
    
    void resetFrame() {
        m_frameArena.reset();
    }
    
private:
    MemorySystem() = default;
    ArenaAllocator m_frameArena;
};

} // namespace td
