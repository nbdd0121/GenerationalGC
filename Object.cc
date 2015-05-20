#include "Object.h"
#include "Heap.h"
#include "debug.h"

#include <cstdio>

using namespace norlit::gc;

Object::Object(Reflection<Object>* reflection):reflection_(reflection) {
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

void* Object::operator new(size_t size) {
    return Heap::Allocate(size);
}

void Object::operator delete(void*) {
    assert(0);
}