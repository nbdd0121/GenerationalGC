#ifndef NORLIT_GC_HEAP_H
#define NORLIT_GC_HEAP_H

#include "Object.h"

namespace norlit {
namespace gc {

struct MemorySpace;

class HeapIterator {
  public:
    virtual void operator()(Object* obj) const = 0;
};

class Heap {
    struct MarkingIterator;
    struct UpdateIterator;
    struct IncRefIterator;
    struct DecRefIterator;
    struct WeakRefNotifyIterator;
    template<typename T>
    class Iterable;
    class StackSpaceIterator;
    class MemorySpaceIterator;
    class LargeObjectSpaceIterator;

    static const size_t LARGE_OBJECT_THRESHOLD = 4096;
    static const size_t MEMORY_SPACE_SIZE = 1024 * 1024;
    static const size_t TENURED_SPACE_THRESHOLD = 16;

    struct LargeObjectNode {
        LargeObjectNode* prev;
        LargeObjectNode* next;
    };
    static bool initialized;
    static Object stack_space;
    static LargeObjectNode large_object_space;
    static MemorySpace* eden_space;
    static MemorySpace* survivor_from_space;
    static MemorySpace* survivor_to_space;
    static MemorySpace* tenured_space;

    // Size of allocating object. Passed from Allocate() to Initialize()
    static uint32_t allocating_size;
    // Suggest a full gc is needed. Set when tenured space is expanded
    static bool full_gc_suggested;
    static uintptr_t no_gc_counter;

    static void GlobalInitialize();
    static void GlobalDestroy();

    static void Minor_ScanRoot(MemorySpace* space);
    static void Major_ScanHeapRoot();
    static void Major_CleanLargeObject();

    // Minor/Major GC indepedent methods
    template<typename I>
    static bool Mark(Iterable<I> iter);
    template<typename I>
    static void Finalize(Iterable<I> iter);
    template<bool asRoot, typename I>
    static void NotifyWeakReference(Iterable<I> iter);

    static void UpdateStackReference();
    template<typename I>
    static void UpdateNonRootReference(Iterable<I> iter);
    template<typename I>
    static void UpdateNonStackRootReference(Iterable<I> iter);


    static void MemorySpace_Copy(MemorySpace* space);
    static void MemorySpace_Move(MemorySpace* space);

    static void PromoteToTenuredSpace(Object* object);

    static void EdenSpace_CalculateTarget();
    static void SurvivorSpace_CalculateTarget();
    static void TenuredSpace_CalculateTarget();

    static void UntrackStackObject(Object* object);
    static void Initialize(Object* object);
    static void* Allocate(size_t size);
  public:
    static void MinorGC();
    static void MajorGC();
    static void Dump(const HeapIterator&);

    friend class Object;
    friend class NoGC;
};

class NoGC {
  public:
    NoGC() {
        Heap::no_gc_counter++;
    }
    ~NoGC() {
        Heap::no_gc_counter--;
    }
    NoGC(const NoGC&) = delete;
    void operator =(const NoGC&) = delete;
};

}
}

#endif