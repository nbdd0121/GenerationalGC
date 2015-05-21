#ifndef NORLIT_GC_ARRAY_H
#define NORLIT_GC_ARRAY_H

#include "Object.h"
#include "Handle.h"
#include <new>

namespace norlit {
namespace gc {
namespace detail {
class ArrayBase: public Object {
    size_t length;
    Object* slots[1];
  protected:
    static void* operator new(size_t size) = delete;
    static void* operator new(size_t size, size_t length, bool);
    static void operator delete(void*, size_t, bool);
    using Object::operator delete;

    ArrayBase(size_t length);

    void Put(size_t index, Object* obj) {
        WriteBarrier(&slots[index], obj);
    }

    Object* Get(size_t index) {
        return slots[index];
    }

    size_t Length() {
        return length;
    }

    virtual void IterateField(const FieldIterator&) override;
};
}

template<typename T>
class Array : public detail::ArrayBase {
    Array(size_t length) : ArrayBase(length) {}
  public:
    void Put(size_t index, const Handle<T>& obj) {
        ArrayBase::Put(index, obj);
    }

    Handle<T> Get(size_t index) {
        ArrayBase::Get(index);
    }

    size_t Length() {
        return ArrayBase::Length();
    }

    static Handle<Array> New(size_t length) {
        return new(length, false)Array(length);
    }
};

template<typename T>
class ValueArray : public Object {
    size_t length;
    char slots[1];
  protected:
    static void* operator new(size_t size) = delete;
    static void* operator new(size_t size, size_t length, bool) {
        return Object::operator new(size + sizeof(T) * length);
    }
    static void operator delete(void*, size_t, bool) {
        assert(0);
    }
    using Object::operator delete;

    ValueArray(size_t length) :length(length) {
        for (size_t i = 0; i < length; i++) {
            new(&At(i)) T();
        }
    }

    virtual ~ValueArray() {
        for (size_t i = 0; i < length; i++) {
            At(i).~T();
        }
    }

  public:
    T& At(size_t index) {
        return reinterpret_cast<T*>(slots)[index];
    }

    size_t Length() {
        return length;
    }

    static Handle<ValueArray> New(size_t length) {
        return new(length, false)ValueArray(length);
    }
};


}
}

#endif