#include "platform.h"

#include <cstdlib>
#include <new>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

using namespace norlit::gc;

void* Platform::Allocate(size_t size) {
#ifdef _WIN32
    void* addr = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!addr) {
        throw std::bad_alloc{};
    }
    return addr;
#else
    void* addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if(addr == MAP_FAILED) {
        throw std::bad_alloc{};
    }
    return addr;
#endif
}

void Platform::Free(void* ptr, size_t size) {
#ifdef _WIN32
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    munmap(ptr, size);
#endif
}