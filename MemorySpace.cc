#include "debug.h"
#include "MemorySpace.h"
#include "Platform.h"

#include <cstring>
#include <new>

using namespace norlit::gc;

MemorySpace::MemorySpace(size_t capacity) :capacity(capacity) {
    top = reinterpret_cast<char*>(data)-reinterpret_cast<char*>(this);
    topOriginal = top;
}

void* MemorySpace::Allocate(size_t size, bool expand) {
    assert((size & 7) == 0);
    if (top + size > capacity) {
        if (!next) {
            if (!expand) {
                return nullptr;
            }
            next = New(capacity);
            void* ret = next->Allocate(size);
            assert(ret);
            return ret;
        }
        return next->Allocate(size, expand);
    }
    void* ret = reinterpret_cast<char*>(this) + top;
    top += size;
    return ret;
}

MemorySpace* MemorySpace::New(size_t capacity) {
    return new(Platform::Allocate(capacity))MemorySpace(capacity);
}

void MemorySpace::FillUnallocated(uint8_t data) {
    memset(End(), data, capacity - top);
    if (next) {
        next->FillUnallocated(data);
    }
}

void MemorySpace::Destroy() {
    if (next) {
        next->Destroy();
    }
    Platform::Free(this, capacity);
}

void MemorySpace::Trim(size_t allowedBlankSpace) {
    if (next) {
        bool nextBlank = next->Begin() == next->End();
        if (nextBlank) {
            if (allowedBlankSpace) {
                next->Trim(allowedBlankSpace - 1);
            } else {
                next->Trim();
                MemorySpace* newNext = next->next;
                next->next = nullptr;
                next->Destroy();
                next = newNext;
            }
        } else {
            next->Trim(allowedBlankSpace);
        }
    }
}