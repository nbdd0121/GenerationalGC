#ifndef NORLIT_GC_OBJECT_H
#define NORLIT_GC_OBJECT_H

#include "common.h"
#include "debug.h"

#include <cstdint>
#include <cstddef>

namespace norlit {
namespace gc {

class Object;

// An overloaded functor that iterates through fields of an object
class FieldIterator {
  public:
    static class {} weak;

    virtual void operator()(Object** field) const = 0;
    virtual void operator()(Object** field, decltype(weak)) const = 0;

    template<typename T>
    void operator()(T** field) const {
        operator()(reinterpret_cast<Object**>(field));
    }
};

class Object {
  private:
    union {
        struct {
            // Double linked list that connects all stack objects
            Object* prev_;
            Object* next_;
        } stack_;

        // Data if object is on heap
        struct {
            Object* dest_;
            // Reference counting from stack space
            uint32_t refcount_;
            uint32_t size_;
        };
    };

    // Place where the object is located at
    Space  space_;
    // GC status of the object
    Status status_;
    // # of gcs the object survived
    uint8_t lifetime_;

    inline bool IsTagged();

    inline void IncRefCount();
    inline void DecRefCount();

    void SlowWriteBarrier(Object** slot, Object* data);

  protected:
    inline void WriteBarrier(Object** slot, Object* data);
    template<typename T>
    inline void WriteBarrier(T** slot, T* data);

    virtual void IterateField(const FieldIterator&);

  public:
    Object();
    virtual ~Object();

    // TODO: Implement copy-ctor and move-ctor for stack objects?
    Object(const Object& obj) = delete;
    void operator =(const Object&) = delete;

    static void* operator new(size_t);
    static void operator delete(void*);

    friend class Heap;
};

inline bool Object::IsTagged() {
    return (reinterpret_cast<uintptr_t>(this) & 7) != 0;
}

inline void Object::WriteBarrier(Object** slot, Object* data) {
    switch (space_) {
        case Space::EDEN_SPACE:
        case Space::SURVIVOR_SPACE:
            *slot = data;
            break;
        default:
            SlowWriteBarrier(slot, data);
            break;
    }
}

template<typename T>
inline void Object::WriteBarrier(T** slot, T* data) {
    WriteBarrier(reinterpret_cast<Object**>(slot), static_cast<Object*>(data));
}

inline void Object::IncRefCount() {
    assert(space_ != Space::STACK_SPACE);
    refcount_++;
}

inline void Object::DecRefCount() {
    assert(space_ != Space::STACK_SPACE);
    refcount_--;
}

}
}

#endif