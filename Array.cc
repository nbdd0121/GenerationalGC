#include "Array.h"

using namespace norlit::gc;
using namespace norlit::gc::detail;

void* ArrayBase::operator new(size_t size, size_t length) {
    return Object::operator new(size + sizeof(Object*) * (length - 1));
}

void ArrayBase::operator delete(void*, size_t) {
    assert(0);
}

ArrayBase::ArrayBase(size_t length) :length(length) {
    for (size_t i = 0; i < length; i++) {
        slots[i] = nullptr;
    }
}

void ArrayBase::IterateField(const FieldIterator& iter) {
    for (size_t i = 0; i < length; i++) {
        iter(&slots[i]);
    }
}