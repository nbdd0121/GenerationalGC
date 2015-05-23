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

    Object* Get() const {
        return object_?*object_:nullptr;
    }
};

}

template<typename T>
class Handle: detail::HandleBase {
  public:
    Handle() : HandleBase() {}
    Handle(std::nullptr_t) : HandleBase() {}
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
        return static_cast<T*>(Get());
    }

    explicit operator bool() const {
        return !!operator T*();
    }

    operator T*() const {
        return static_cast<T*>(Get());
    }

    template<typename D>
    explicit operator D*() const {
        return static_cast<D*>(operator T*());
    }
};


}
}

#endif