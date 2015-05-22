#include "Object.h"
#include "Heap.h"
#include "debug.h"
#include "Handle.h"

#include <cstdio>

using namespace norlit::gc;

decltype(FieldIterator::weak) FieldIterator::weak;

Object::Object() {
    Heap::Initialize(this);
}

Object::~Object() {
    if (space_ == Space::STACK_SPACE) {
        Heap::UntrackStackObject(this);
    }
}

void Object::SlowWriteBarrier(Object** slot, Object* data) {
    switch (space_) {
        case Space::STACK_SPACE:
        case Space::TENURED_SPACE:
        case Space::LARGE_OBJECT_SPACE:
            if (data && !data->IsTagged()) {
                data->IncRefCount();
            }
            if (*slot && !(*slot)->IsTagged()) {
                (*slot)->DecRefCount();
            }
            *slot = data;
            return;
        default:
            assert(0);
    }
}

void Object::IterateField(const FieldIterator& iter) {

}

void Object::NotifyWeakReferenceCollected(Object** slot) {

}

void* Object::operator new(size_t size) {
    return Heap::Allocate(size);
}

void Object::operator delete(void*) {
    assert(0);
}

uintptr_t Object::HashCode() {
    return reinterpret_cast<uintptr_t>(this);
}

bool Object::Equals(const Handle<Object>& obj) {
    return this == obj;
}