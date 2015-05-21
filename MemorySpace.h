#ifndef NORLIT_GC_MEMORYSPACE_H
#define NORLIT_GC_MEMORYSPACE_H

#include <cstdint>
#include <cstddef>

namespace norlit {
namespace gc {

struct MemorySpace {
    static MemorySpace* New(size_t capacity);

    uintptr_t top;
    uintptr_t capacity;
    uintptr_t topOriginal;
    MemorySpace* next = nullptr;
    uintptr_t data[1];

    MemorySpace(size_t capacity);

    void FillUnallocated(uint8_t);
    void Destroy();
    void Trim(size_t = 0);
    void* Allocate(size_t size, bool expand = false);

    inline void Clear();
    inline void SaveOriginal();
    inline char* End();
    inline char* Begin();
    inline char* OriginalEnd();
};

inline void MemorySpace::SaveOriginal() {
    topOriginal = top;
    if (next) {
        next->SaveOriginal();
    }
}

inline char* MemorySpace::OriginalEnd() {
    return reinterpret_cast<char*>(this) + topOriginal;
}

inline char* MemorySpace::Begin() {
    return reinterpret_cast<char*>(data);
}

inline char* MemorySpace::End() {
    return reinterpret_cast<char*>(this) + top;
}

inline void MemorySpace::Clear() {
    top = reinterpret_cast<char*>(data)-reinterpret_cast<char*>(this);
    if (next) {
        next->Clear();
    }
}


}
}

#endif