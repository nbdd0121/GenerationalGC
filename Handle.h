#ifndef NORLIT_GC_HANDLE_H
#define NORLIT_GC_HANDLE_H

#include "Object.h"

#include <utility>

namespace norlit {
namespace gc {
namespace detail {

class HandleBase {
    Object** object_;

  protected:
    HandleBase();
    HandleBase(Object* obj);
    HandleBase(const HandleBase& obj);
    HandleBase(HandleBase&& obj);
    ~HandleBase();

    void operator =(Object* obj);
    void operator =(const HandleBase& obj);
    void operator =(HandleBase&& obj);

    Object* operator *() const {
        return object_?*object_:nullptr;
    }
};

}

template<typename T>
class Handle: public detail::HandleBase {
  public:
    Handle() : HandleBase() {}
    Handle(T* obj) :HandleBase(obj) {}
    Handle(const Handle& h) :HandleBase(h) {}
    Handle(Handle&& h) :HandleBase(std::move(h)) {}

    void operator =(T* obj) {
        HandleBase::operator =(obj);
    }

    void operator =(const Handle& h) {
        HandleBase::operator =(h);
    }

    void operator =(Handle&& h) {
        HandleBase::operator =(std::move(h));
    }

    T* operator ->() const {
        return static_cast<T*>(HandleBase::operator*());
    }

    operator T*() const {
        return static_cast<T*>(HandleBase::operator*());
    }

    template<typename D>
    explicit operator D*() const {
        return static_cast<D*>(operator T*());
    }
};


}
}

#endif