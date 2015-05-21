#include "common.h"
#include "debug.h"

#include "Heap.h"
#include "MemorySpace.h"
#include "Platform.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <algorithm>

using namespace norlit::gc;

struct Heap::MarkingIterator : public FieldIterator {
    virtual void operator()(Object** field) const {
        Object* obj = *field;
        if (!obj || obj->IsTagged()) {
            return;
        }
        assert(obj->space_ != Space::STACK_SPACE);
        if (obj->status_ == Status::NOT_MARKED) {
            obj->status_ = Status::MARKING;
        }
    }

    virtual void operator()(Object** field, decltype(weak)) const {}
};

struct Heap::UpdateIterator : public FieldIterator {
    virtual void operator()(Object** field) const {
        Object* obj = *field;
        if (!obj || obj->IsTagged()) {
            return;
        }
        assert(obj->space_ != Space::STACK_SPACE);
        *field = obj->dest_;
    }

    virtual void operator()(Object** field, decltype(weak)) const {
        operator()(field);
    }
};

struct Heap::IncRefIterator : public FieldIterator {
    virtual void operator()(Object** field) const {
        Object* obj = *field;
        if (!obj || obj->IsTagged()) {
            return;
        }
        assert(obj->space_ != Space::STACK_SPACE);
        obj->IncRefCount();
    }

    virtual void operator()(Object** field, decltype(weak)) const {}
};

struct Heap::DecRefIterator : public FieldIterator {
    virtual void operator()(Object** field) const {
        Object* obj = *field;
        if (!obj || obj->IsTagged()) {
            return;
        }
        assert(obj->space_ != Space::STACK_SPACE);
        obj->DecRefCount();
    }

    virtual void operator()(Object** field, decltype(weak)) const {}
};

struct Heap::WeakRefNotifyIterator : public FieldIterator {
    Object* target;

    WeakRefNotifyIterator(Object* target) :target(target) {}

    virtual void operator()(Object** field) const {}

    virtual void operator()(Object** field, decltype(weak)) const {
        Object* obj = *field;
        if (!obj || obj->IsTagged()) {
            return;
        }
        assert(obj->space_ != Space::STACK_SPACE);
        if (!obj->dest_) {
            *field = nullptr;
            target->NotifyWeakReferenceCollected(field);
        }
    }
};

// In order to make MemorySpace implementation simple and Object-detail free,
// we make the walker part of implementation of Heap.
// The heap uses Java-style iterator model with a glue layer make it work with
// c++11 range-based for loop
class Heap::MemorySpaceIterator {
    // Object pointer
    Object* objectPtr;
    Object* objectEnd;
    MemorySpace* space;
    bool useOriginal;

  public:
    MemorySpaceIterator(MemorySpace* space, bool original = false) :useOriginal(original) {
        LoadSpace(space);
    }

    void LoadSpace(MemorySpace* space) {
        this->space = space;
        objectPtr = reinterpret_cast<Object*>(space->Begin());
        objectEnd = reinterpret_cast<Object*>(useOriginal ? space->OriginalEnd() : space->End());
    }

    bool HasNext() {
        if (objectPtr < objectEnd) {
            return true;
        }
        while (space->next) {
            LoadSpace(space->next);
            if (objectPtr < objectEnd) {
                return true;
            }
        }
        return false;
    }

    Object* Next() {
        Object* ret = objectPtr;
        objectPtr = reinterpret_cast<Object*>(reinterpret_cast<char*>(objectPtr)+objectPtr->size_);
        return ret;
    }
};

class Heap::StackSpaceIterator {
    // Use of prefetch here allows node to be deleted during iteration
    Object* next;

  public:
    StackSpaceIterator() {
        next = stack_space.stack_.next_;
    }

    bool HasNext() {
        return next != &stack_space;
    }

    Object* Next() {
        Object* ret = next;
        next = next->stack_.next_;
        return ret;
    }

};

class Heap::LargeObjectSpaceIterator {
    // Use of prefetch here allows node to be deleted during iteration
    LargeObjectNode* current;
    LargeObjectNode* next;

  public:
    LargeObjectSpaceIterator() {
        current = nullptr;
        next = large_object_space.next;
    }

    bool HasNext() {
        return next != &large_object_space;
    }

    Object* Next() {
        current = next;
        next = current->next;
        return reinterpret_cast<Object*>(current + 1);
    }

    // Remove a large object from queue and free it
    void Remove() {
        current->prev->next = next;
        next->prev = current->prev;
        Platform::Free(current, sizeof(LargeObjectNode) + reinterpret_cast<Object*>(current + 1)->size_);
        current = nullptr;
    }
};

template<typename T>
class Heap::Iterable {
    T t;

    class Iterator {
        Iterable* iter;
      public:
        Iterator(Iterable* iter) :iter(iter) {}
        bool operator !=(const Iterator&) {
            return iter->t.HasNext();
        }
        void operator ++() {}
        Object* operator *() {
            return iter->t.Next();
        }
    };

  public:
    template<typename... Args>
    Iterable(Args&&... args) : t(std::forward<Args>(args)...) {}
    Iterator begin() {
        return this;
    }
    Iterator end() {
        return this;
    }
};

Object Heap::stack_space{};
Heap::LargeObjectNode Heap::large_object_space{
    &large_object_space,
    &large_object_space
};
MemorySpace* Heap::eden_space;
MemorySpace* Heap::survivor_from_space;
MemorySpace* Heap::survivor_to_space;
MemorySpace* Heap::tenured_space;
uint32_t Heap::allocating_size = 0;
bool Heap::full_gc_suggested = false;
uintptr_t Heap::no_gc_counter = 0;

void Heap::GlobalInitialize() {
    eden_space = MemorySpace::New(MEMORY_SPACE_SIZE);
    survivor_from_space = MemorySpace::New(MEMORY_SPACE_SIZE);
    survivor_to_space = MemorySpace::New(MEMORY_SPACE_SIZE);
    tenured_space = MemorySpace::New(MEMORY_SPACE_SIZE);
#if NORLIT_DEBUG_MODE
    eden_space->FillUnallocated(0xCC);
    survivor_from_space->FillUnallocated(0xCC);
    survivor_to_space->FillUnallocated(0xCC);
    tenured_space->FillUnallocated(0xCC);
#endif
}

void Heap::GlobalDestroy() {
    eden_space->Destroy();
    survivor_from_space->Destroy();
    survivor_to_space->Destroy();
    tenured_space->Destroy();

    // Destroy Large Object Space
    for (LargeObjectSpaceIterator iter;
            iter.HasNext();
            iter.Next(), iter.Remove());
}

void* Heap::Allocate(size_t size) {
    if (size > 0xFFFFFFFF) {
        throw std::bad_alloc{};
    }

    // Align to 8 bytes
    size = (size + 7) &~7;

    // Set allocating_size so Heap::Initialize can receive info
    allocating_size = static_cast<uint32_t>(size);

    if (size > LARGE_OBJECT_THRESHOLD) {
        // We cannot start GC if no_gc_counter is non-zero
        if (!no_gc_counter && full_gc_suggested) {
            MajorGC();
            full_gc_suggested = false;
        } else {
            full_gc_suggested = true;
        }

        LargeObjectNode* node = static_cast<LargeObjectNode*>(Platform::Allocate(sizeof(LargeObjectNode) + size));
        node->prev = large_object_space.prev;
        node->next = &large_object_space;
        large_object_space.prev->next = node;
        large_object_space.prev = node;

        void* ret = static_cast<void*>(node + 1);
        debug("A new large object is allocated on %p\n", ret);
        return ret;
    }

    void* ret = eden_space->Allocate(size);
    if (!ret) {
        debug("Reason: Eden space out of memory\n");
        if (!no_gc_counter) {
            if (full_gc_suggested) {
                MajorGC();
                full_gc_suggested = false;
            } else {
                MinorGC();
            }
            ret = eden_space->Allocate(size);
            // This should never happen. Eden space is already cleared
            assert(ret);
        } else {
            // Since Survivor Space can extend,
            // we need to allocate them directly in survivor space
            ret = survivor_from_space->Allocate(size, true);
            debug("GC cannot trigger. Allocate on Survivor Space\n");
        }
    }
    debug("A new object is allocated on %p\n", ret);
    return ret;
}

void Heap::Initialize(Object* object) {
    // If allocation is on stack
    if (allocating_size == 0) {
        if (object == &stack_space) {
            // This means static variable's constructor is called, so prepare everything
            GlobalInitialize();
            object->stack_.prev_ = object;
            object->stack_.next_ = object;
        } else {
            // Add the object to the double linked list
            object->stack_.prev_ = stack_space.stack_.prev_;
            object->stack_.next_ = &stack_space;
            stack_space.stack_.prev_->stack_.next_ = object;
            stack_space.stack_.prev_ = object;
        }
        // We only care space_ for stack objects. Everything else is for heap objects
        object->space_ = Space::STACK_SPACE;
        return;
    }

    if (allocating_size > LARGE_OBJECT_THRESHOLD) {
        // Large object will never be moved
        object->dest_ = object;
        object->space_ = Space::LARGE_OBJECT_SPACE;
    } else if (
        !no_gc_counter || (
            // If no_gc_counter is true we need to have an extra check to see if
            // object is in eden space
            reinterpret_cast<char*>(object) >= eden_space->Begin() &&
            reinterpret_cast<char*>(object) < eden_space->End()
        )) {
        object->space_ = Space::EDEN_SPACE;
    } else {
        object->space_ = Space::SURVIVOR_SPACE;
    }

    object->refcount_ = 0;
    object->size_ = allocating_size;
    object->status_ = Status::NOT_MARKED;
    object->lifetime_ = 0;
    allocating_size = 0;
}

void Heap::UntrackStackObject(Object* object) {
    if (object == &stack_space) {
        // If it's the global destructor, clean up everything
        GlobalDestroy();
        return;
    }

    // When we release a stack object, we decrease refcount,
    // otherwise the referenced object can only be reclaimed in MajorGC
    object->IterateField(DecRefIterator{});
    // Detach from linked list
    object->stack_.prev_->stack_.next_ = object->stack_.next_;
    object->stack_.next_->stack_.prev_ = object->stack_.prev_;
}

void Heap::Minor_ScanRoot(MemorySpace* space) {
    // In minor GC, the "root" are actually objects referenced by
    // real roots and tenured object
    for (Object* object : Iterable<MemorySpaceIterator> {space}) {
        if (object->refcount_) {
            object->status_ = Status::MARKING;
        }
    }
}

void Heap::Major_ScanHeapRoot() {
    // In major GC, the "root" are objects referenced by real roots
    for (Object* object = stack_space.stack_.next_;
            object != &stack_space;
            object = object->stack_.next_) {
        object->IterateField(MarkingIterator{});
    }
}

template<typename I>
bool Heap::Mark(Iterable<I> iter) {
    // Classic mark algorithm
    bool modified = false;
    for (Object* object : iter) {
        if (object->status_ == Status::MARKING) {
            modified = true;
            object->IterateField(MarkingIterator{});
            object->status_ = Status::MARKED;
        }
    }
    return modified;
}

template<typename I>
void Heap::Finalize(Iterable<I> iter) {
    // Calling destructors
    for (Object* object : iter) {
        if (object->status_ != Status::MARKED) {
            object->~Object();
            // We set object->dest_ here because LargeObjectSpace
            // do not have a pass that helps set dest_ field.
            object->dest_ = nullptr;
        }
    }
}

template<bool asRoot, typename I>
void Heap::NotifyWeakReference(Iterable<I> iter) {
    for (Object* object : iter) {
        if (asRoot || object->status_ == Status::MARKED) {
            object->IterateField(WeakRefNotifyIterator{ object });
        }
    }
}

void Heap::UpdateReference(MemorySpace* space) {
    // Update reference using object.dest_
    for (Object* object : Iterable<MemorySpaceIterator> { space }) {
        if (object->status_ == Status::MARKED) {
            object->IterateField(UpdateIterator{});
        }
    }
}

void Heap::Minor_UpdateTenuredReference() {
    // We use OriginalEnd here, since we might promote a object in previous calls
    // And we reset object.status_, because they might be changed by MarkingIterator
    // Since it is minor GC, tenured objects are not reclaimed, so object.status_ is not checked
    MemorySpace* space = tenured_space;
    Object* object;
    char* objectPtr;
    do {
        for (objectPtr = space->Begin(), object = reinterpret_cast<Object*>(objectPtr);
                objectPtr < space->OriginalEnd();
                objectPtr += object->size_, object = reinterpret_cast<Object*>(objectPtr)) {
            object->status_ = Status::NOT_MARKED;
            object->IterateField(UpdateIterator{});
        }
        space = space->next;
    } while (space);
}

void Heap::Minor_UpdateLargeObjectReference() {
    // Similar to Tenured Space, but we iterate through large object space here
    for (Object* object : Iterable<LargeObjectSpaceIterator> {}) {
        object->status_ = Status::NOT_MARKED;
        object->IterateField(UpdateIterator{});
    }
}

void Heap::Major_UpdateTenuredReference() {
    // In major GC, we also use OriginalEnd because we move & promote objects
    // We need to check object.status_ now because some of tenured objects might
    // be collected. We do not reset object.status_ because they are used and reseted
    // by following calls
    MemorySpace* space = tenured_space;
    Object* object;
    char* objectPtr;
    do {
        for (objectPtr = space->Begin(), object = reinterpret_cast<Object*>(objectPtr);
                objectPtr < space->OriginalEnd();
                objectPtr += object->size_, object = reinterpret_cast<Object*>(objectPtr)) {
            if (object->status_ == Status::MARKED) {
                object->IterateField(UpdateIterator{});
            }
        }
        space = space->next;
    } while (space);
}

void Heap::Major_UpdateLargeObjectReference() {
    // Similar to Tenured Space, but we iterate through large object space here
    for (Object* object : Iterable<LargeObjectSpaceIterator> {}) {
        if (object->status_ == Status::MARKED) {
            object->IterateField(UpdateIterator{});
        }
    }
}

void Heap::MemorySpace_Copy(MemorySpace* space) {
    // Used for Eden Space and Survivor Space (mark-copy)
    for (Object* object : Iterable<MemorySpaceIterator> { space }) {
        if (object->status_ == Status::MARKED) {
            object->status_ = Status::NOT_MARKED;
            memcpy(object->dest_, object, object->size_);
        }
    }
}


void Heap::MemorySpace_Move(MemorySpace* space) {
    // Used for Tenured Space (mark-compact)
    Object* object;
    char* objectPtr, *prefetch;
    do {
        for (objectPtr = space->Begin(), object = reinterpret_cast<Object*>(objectPtr), prefetch = objectPtr+object->size_;
                objectPtr < space->OriginalEnd();
                objectPtr = prefetch, object = reinterpret_cast<Object*>(objectPtr), prefetch = objectPtr + object->size_) {
            if (object->status_ == Status::MARKED) {
                object->status_ = Status::NOT_MARKED;
                memmove(object->dest_, object, object->size_);
            }
        }
        space = space->next;
    } while (space);
}

void Heap::UpdateStackReference() {
    // Update references on stack
    for (Object* object = stack_space.stack_.next_;
            object != &stack_space;
            object = object->stack_.next_) {
        object->IterateField(UpdateIterator{});
    }
}

void Heap::EdenSpace_CalculateTarget() {
    // Calculate target address for Eden Space.
    // This is simple because the only target is Survivor Space
    for (Object* object : Iterable<MemorySpaceIterator> { eden_space }) {
        if (object->status_ == Status::MARKED) {
            // All Eden Space objects that survives a minor GC will be moved to survivor space
            object->dest_ = static_cast<Object*>(
                                survivor_to_space->Allocate(object->size_, true)
                            );
            debug("Object %p [Eden] is moved to %p [Survivor]\n", object, object->dest_);
            object->space_ = Space::SURVIVOR_SPACE;
            object->lifetime_++;
        } else {
            debug("Reclaim %p\n", object);
            // dest_ is set in Finalize
        }
    }
}

void Heap::PromoteToTenuredSpace(Object *object) {
    // Promote an object from survivor space to tenured space
    void* target = tenured_space->Allocate(object->size_);
    if (!target) {
        full_gc_suggested = true;
        target = tenured_space->Allocate(object->size_, true);
    }
    object->dest_ = static_cast<Object*>(target);
    debug("Object %p [Survivor] is promoted to %p [Tenure]\n", object, object->dest_);
    object->space_ = Space::TENURED_SPACE;
    // Since in tenured space, we use reference counting to avoid the marking phase, we increase ref here
    object->IterateField(IncRefIterator{});
}

void Heap::SurvivorSpace_CalculateTarget() {
    MemorySpace* space = survivor_from_space;
    for (Object* object : Iterable<MemorySpaceIterator> { space }) {
        if (object->status_ == Status::MARKED) {
            // Promote an object that survives many times of GC
            if (object->lifetime_ > TENURED_SPACE_THRESHOLD) {
                PromoteToTenuredSpace(object);
            } else {
                // Objects that survives less than THRESHOLD times GC will remain in survivor space
                object->dest_ = static_cast<Object*>(
                                    survivor_to_space->Allocate(object->size_, true)
                                );
                debug("Object %p [Survivor] is moved to %p [Survivor]\n", object, object->dest_);
                object->lifetime_++;
            }
        } else {
            debug("Reclaim %p\n", object);
            // dest_ is set in Finalize
        }
    }
}

void Heap::TenuredSpace_CalculateTarget() {
    MemorySpace* space = tenured_space;
    Object* object;
    char* objectPtr;
    do {
        for (objectPtr = space->Begin(), object = reinterpret_cast<Object*>(objectPtr);
                objectPtr < space->OriginalEnd();
                objectPtr += object->size_, object = reinterpret_cast<Object*>(objectPtr)) {
            if (object->status_ == Status::MARKED) {
                object->dest_ = static_cast<Object*>(
                                    tenured_space->Allocate(object->size_, true)
                                );
                debug("Object %p [Tenured] is moved to %p [Tenured]\n", object, object->dest_);
            } else {
                // When tenured objects are collected, decrease ref to
                // allow referenced young objects to be recycled in minor GC
                object->IterateField(DecRefIterator{});
                debug("Reclaim Tenured %p\n", object);
                // dest_ is set in Finalize
            }
        }
        space = space->next;
    } while (space);
}

void Heap::Major_CleanLargeObject() {
    LargeObjectSpaceIterator iterator;
    while (iterator.HasNext()) {
        Object* object = iterator.Next();
        if (object->status_ != Status::MARKED) {
            debug("Reclaim Large Object %p\n", object);
            iterator.Remove();
        }
    }
}

void Heap::MinorGC() {
    if (no_gc_counter) {
        throw std::runtime_error{"Minor GC triggered in NoGC scope"};
    }
    debug("----- Minor GC -----\n");
    // Use reference count number assigned by root and tenured generation
    Minor_ScanRoot(eden_space);
    Minor_ScanRoot(survivor_from_space);

    // Mark. Note that this step will cause some tenured space's objects to be marked as "MARKED"
    // TODO: This is super inefficient, use a queue to speed it up
    while (
        Mark<MemorySpaceIterator>(eden_space) |
        Mark<MemorySpaceIterator>(survivor_from_space)
    );

    Finalize<MemorySpaceIterator>(eden_space);
    Finalize<MemorySpaceIterator>(survivor_from_space);

    // In case that we may expand tenured space, we need to save it for
    // Minor_UpdateTenuredReference
    tenured_space->SaveOriginal();

    // Calculate move target
    EdenSpace_CalculateTarget();
    SurvivorSpace_CalculateTarget();

    // Weak references holders, if their referred object is collected, will be notified
    // as Java's Reference queue works
    NotifyWeakReference<false, MemorySpaceIterator>(eden_space);
    NotifyWeakReference<false, MemorySpaceIterator>(survivor_from_space);
    // OriginalEnd() is required because we already modified the space
    NotifyWeakReference<true, MemorySpaceIterator>({tenured_space, true});
    NotifyWeakReference<true, LargeObjectSpaceIterator>({});
    NotifyWeakReference<true, StackSpaceIterator>({});

    // Update stack and tenured space reference
    UpdateStackReference();
    UpdateReference(eden_space);
    UpdateReference(survivor_from_space);
    // We clean the mark of "MARKED" in this step
    Minor_UpdateTenuredReference();
    Minor_UpdateLargeObjectReference();

    // Copy
    MemorySpace_Copy(eden_space);
    MemorySpace_Copy(survivor_from_space);

    // Mark as clear for re-using
    eden_space->Clear();
    survivor_from_space->Clear();

#if NORLIT_DEBUG_MODE
    eden_space->FillUnallocated(0xCC);
    survivor_from_space->FillUnallocated(0xCC);
#endif

    std::swap(survivor_from_space, survivor_to_space);

    debug("----- Minor GC Finished -----\n");
}

void Heap::MajorGC() {
    if (no_gc_counter) {
        throw std::runtime_error{ "Major GC triggered in NoGC scope" };
    }
    debug("----- Major GC -----\n");
    // Use reference count number assigned by root and tenured generation
    Major_ScanHeapRoot();

    // Mark
    while (
        Mark<MemorySpaceIterator>(eden_space) |
        Mark<MemorySpaceIterator>(survivor_from_space) |
        Mark<MemorySpaceIterator>(tenured_space) |
        Mark<LargeObjectSpaceIterator>({})
    );

    // Call destructors
    Finalize<MemorySpaceIterator>(eden_space);
    Finalize<MemorySpaceIterator>(survivor_from_space);
    Finalize<MemorySpaceIterator>(tenured_space);
    Finalize<LargeObjectSpaceIterator>({});

    // We clean tenured space, meaning that we are going to compact it
    tenured_space->SaveOriginal();
    tenured_space->Clear();

    // Calculate move target
    EdenSpace_CalculateTarget();
    TenuredSpace_CalculateTarget();
    SurvivorSpace_CalculateTarget();
    // We do not move large target, and their dest_ is set in Finalize<LargeObjectSpaceIterator>({})

    NotifyWeakReference<false, MemorySpaceIterator>(eden_space);
    NotifyWeakReference<false, MemorySpaceIterator>(survivor_from_space);
    NotifyWeakReference<false, MemorySpaceIterator>({tenured_space, true});
    NotifyWeakReference<false, LargeObjectSpaceIterator>({});
    NotifyWeakReference<true, StackSpaceIterator>({});

    // Update stack and tenured space reference
    UpdateStackReference();
    UpdateReference(eden_space);
    UpdateReference(survivor_from_space);
    Major_UpdateTenuredReference();
    Major_UpdateLargeObjectReference();

    // Copy
    MemorySpace_Copy(eden_space);
    MemorySpace_Move(tenured_space);
    MemorySpace_Copy(survivor_from_space);
    Major_CleanLargeObject();

    // Mark as clear for re-using
    eden_space->Clear();
    survivor_from_space->Clear();

#if NORLIT_DEBUG_MODE
    eden_space->FillUnallocated(0xCC);
    survivor_from_space->FillUnallocated(0xCC);
    tenured_space->FillUnallocated(0xCC);
#endif

    std::swap(survivor_from_space, survivor_to_space);

    debug("----- Major GC Finished -----\n");
}