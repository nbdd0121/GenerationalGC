#include "Handle.h"

namespace norlit {
namespace gc {
struct HandleReflection;
}
}

using namespace norlit::gc;

struct norlit::gc::HandleReflection : public Reflection<HandleBase> {
    static HandleReflection instance;

    virtual void IterateField(HandleBase* h, const FieldIterator& iter) {
        iter(&h->object_);
    }
};

HandleReflection HandleReflection::instance;

HandleBase::HandleBase(Object* obj) :Object(&HandleReflection::instance) {
    WriteBarrier(&object_, obj);
}