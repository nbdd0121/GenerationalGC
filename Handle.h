#ifndef NORLIT_GC_HANDLE_H
#define NORLIT_GC_HANDLE_H

#include "Object.h"
#include "Reflection.h"

namespace norlit {
namespace gc {

class HandleBase: public Object {
    Object* object_ = nullptr;

  protected:
    HandleBase(Object* obj);

    Object* object() {
        return object_;
    }

    void set_object(Object* obj) {
        WriteBarrier(&object_, obj);
    }

    friend struct HandleReflection;
};

template<typename T>
class Handle: public HandleBase {
  public:
    Handle(T* obj) :HandleBase(obj) {

    }

    T* operator ->() {
        return static_cast<T*>(object());
    }

    operator T*() {
        return static_cast<T*>(object());
    }

    void operator =(T* obj) {
        set_object(obj);
    }
};


}
}

#endif