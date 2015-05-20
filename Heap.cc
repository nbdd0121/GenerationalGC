#include "common.h"
#include "debug.h"

#include "Heap.h"
#include "MemorySpace.h"
#include "Platform.h"

#include <cstdio>
#include <cstring>
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
            obj->status_ = Status::MARKED;
        }
    }

    virtual void operator()(Object** field, decltype(weak)) const {
    }
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

    virtual void operator()(Object** field, decltype(weak)) const {
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
size_t Heap::allocating_size = 0;
bool Heap::full_gc_suggested = false;

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
    for (LargeObjectNode* node = large_object_space.next, *prefetch = node->next;
            node != &large_object_space;
            node = prefetch, prefetch = node->next) {
        Object* object = reinterpret_cast<Object*>(node + 1);
        Platform::Free(node, sizeof(LargeObjectNode) + object->size_);
    }
}

void* Heap::Allocate(size_t size) {
    // Align to 8 bytes
    size = (size + 7) &~7;

    if (size > LARGE_OBJECT_THRESHOLD) {
        if (full_gc_suggested) {
            MajorGC();
            full_gc_suggested = false;
        } else {
            full_gc_suggested = true;
        }

        allocating_size = size;

        LargeObjectNode* node = static_cast<LargeObjectNode*>(Platform::Allocate(sizeof(LargeObjectNode) + size));
        node->prev = large_object_space.prev;
        node->next = &large_object_space;
        large_object_space.prev->next = node;
        large_object_space.prev = node;
        return static_cast<void*>(node+1);
    }

    // Set allocating_size so Heap::Initialize can receive info
    allocating_size = size;
    void* ret = eden_space->Allocate(size);
    if (!ret) {
        debug("Reason: Eden space out of memory\n");
        if (full_gc_suggested) {
            MajorGC();
            full_gc_suggested = false;
        } else {
            MinorGC();
        }
        ret = eden_space->Allocate(size);
        // This should never happen. Eden space is already cleared
        assert(ret);
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
    } else {
        object->space_ = Space::EDEN_SPACE;
    }

    object->refcount_ = 0;
    object->size_ = allocating_size;
    object->space_ = Space::EDEN_SPACE;
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
    Object* object;
    char* objectPtr;
    do {
        for (objectPtr = space->Begin(), object = reinterpret_cast<Object*>(objectPtr);
                objectPtr < space->End();
                objectPtr += object->size_, object = reinterpret_cast<Object*>(objectPtr)) {
            if (object->refcount_) {
                object->status_ = Status::MARKING;
            }
        }
        space = space->next;
    } while (space);
}

void Heap::Major_ScanHeapRoot() {
    // In major GC, the "root" are objects referenced by real roots
    for (Object* object = stack_space.stack_.next_;
            object != &stack_space;
            object = object->stack_.next_) {
        object->IterateField(MarkingIterator{});
    }
}

bool Heap::Mark(MemorySpace* space) {
    // Classic mark algorithm
    bool modified = false;
    Object* object;
    char* objectPtr;
    do {
        for (objectPtr = space->Begin(), object = reinterpret_cast<Object*>(objectPtr);
                objectPtr < space->End();
                objectPtr += object->size_, object = reinterpret_cast<Object*>(objectPtr)) {
            if (object->status_ == Status::MARKING) {
                modified = true;
                object->IterateField(MarkingIterator{});
                object->status_ = Status::MARKED;
            }
        }
        space = space->next;
    } while (space);
    return modified;
}

bool Heap::Major_MarkLargeObject() {
    bool modified = false;
    for (LargeObjectNode* node = large_object_space.next;
            node != &large_object_space;
            node = node->next) {
        Object* object = reinterpret_cast<Object*>(node+1);
        if (object->status_ == Status::MARKING) {
            modified = true;
            object->IterateField(MarkingIterator{});
            object->status_ = Status::MARKED;
        }
    }
    return modified;
}

void Heap::Finialize(MemorySpace* space) {
    // Call destructors
    Object* object;
    char* objectPtr;
    do {
        for (objectPtr = space->Begin(), object = reinterpret_cast<Object*>(objectPtr);
                objectPtr < space->End();
                objectPtr += object->size_, object = reinterpret_cast<Object*>(objectPtr)) {
            if (object->status_ != Status::MARKED) {
                object->~Object();
            }
        }
        space = space->next;
    } while (space);
}

void Heap::Major_FinalizeLargeObject() {
    for (LargeObjectNode* node = large_object_space.next;
            node != &large_object_space;
            node = node->next) {
        Object* object = reinterpret_cast<Object*>(node+1);
        if (object->status_ != Status::MARKED) {
            object->~Object();
            object->dest_ = nullptr;
        }
    }
}

void Heap::UpdateReference(MemorySpace* space) {
    // Update reference using object.dest_
    Object* object;
    char* objectPtr;
    do {
        for (objectPtr = space->Begin(), object = reinterpret_cast<Object*>(objectPtr);
                objectPtr < space->End();
                objectPtr += object->size_, object = reinterpret_cast<Object*>(objectPtr)) {
            if (object->status_ == Status::MARKED) {
                object->IterateField(UpdateIterator{});
            }
        }
        space = space->next;
    } while (space);
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
    for (LargeObjectNode* node = large_object_space.next;
            node != &large_object_space;
            node = node->next) {
        Object* object = reinterpret_cast<Object*>(node+1);
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
    for (LargeObjectNode* node = large_object_space.next;
            node != &large_object_space;
            node = node->next) {
        Object* object = reinterpret_cast<Object*>(node+1);
        if (object->status_ == Status::MARKED) {
            object->IterateField(UpdateIterator{});
        }
    }
}

void Heap::MemorySpace_Copy(MemorySpace* space) {
    // Used for Eden Space and Survivor Space (mark-copy)
    Object* object;
    char* objectPtr;
    do {
        for (objectPtr = space->Begin(), object = reinterpret_cast<Object*>(objectPtr);
                objectPtr < space->End();
                objectPtr += object->size_, object = reinterpret_cast<Object*>(objectPtr)) {
            if (object->status_ == Status::MARKED) {
                object->status_ = Status::NOT_MARKED;
                memcpy(object->dest_, object, object->size_);
            }
        }
        space = space->next;
    } while (space);
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
    Object* object;
    char* objectPtr;
    for (objectPtr = eden_space->Begin(), object = reinterpret_cast<Object*>(objectPtr);
            objectPtr < eden_space->End();
            objectPtr += object->size_, object = reinterpret_cast<Object*>(objectPtr)) {
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
            object->dest_ = nullptr;
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
    Object* object;
    char* objectPtr;
    do {
        for (objectPtr = space->Begin(), object = reinterpret_cast<Object*>(objectPtr);
                objectPtr < space->End();
                objectPtr += object->size_, object = reinterpret_cast<Object*>(objectPtr)) {
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
                object->dest_ = nullptr;
            }
        }
        space = space->next;
    } while (space);
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
                object->dest_ = nullptr;
            }
        }
        space = space->next;
    } while (space);
}

void Heap::Major_CleanLargeObject() {
    for (LargeObjectNode* node = large_object_space.next, *prefetch = node->next;
            node != &large_object_space;
            node = prefetch, prefetch = node->next) {
        Object* object = reinterpret_cast<Object*>(node+1);

        if (object->status_ != Status::MARKED) {
            debug("Reclaim Large Object %p\n", object);
            node->prev->next = node->next;
            node->next->prev = node->prev;
            Platform::Free(node, sizeof(LargeObjectNode) + object->size_);
        }
    }
}

void Heap::MinorGC() {
    debug("----- Minor GC -----\n");
    // Use reference count number assigned by root and tenured generation
    Minor_ScanRoot(eden_space);
    Minor_ScanRoot(survivor_from_space);

    // Mark. Note that this step will cause some tenured space's objects to be marked as "MARKED"
    // TODO: This is super inefficient, use a queue to speed it up
    while (
        Mark(eden_space) |
        Mark(survivor_from_space)
    );

    Finialize(eden_space);
    Finialize(survivor_from_space);

    // In case that we may expand tenured space, we need to save it for
    // Minor_UpdateTenuredReference
    tenured_space->SaveOriginal();

    // Calculate move target
    EdenSpace_CalculateTarget();
    SurvivorSpace_CalculateTarget();

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
    debug("Major GC Triggered\n");
    // Use reference count number assigned by root and tenured generation
    Major_ScanHeapRoot();

    // Mark
    while (
        Mark(eden_space) |
        Mark(survivor_from_space) |
        Mark(tenured_space) |
        Major_MarkLargeObject()
    );

    // Call destructors
    Finialize(eden_space);
    Finialize(survivor_from_space);
    Finialize(tenured_space);
    Major_FinalizeLargeObject();

    // We clean tenured space, meaning that we are going to compact it
    tenured_space->SaveOriginal();
    tenured_space->Clear();

    // Calculate move target
    EdenSpace_CalculateTarget();
    TenuredSpace_CalculateTarget();
    SurvivorSpace_CalculateTarget();
    // We do not move large target, and their dest_ is set in Major_FinalizeLargeObject()

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