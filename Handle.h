#ifndef NORLIT_GC_HANDLE_H
#define NORLIT_GC_HANDLE_H

#include "Object.h"

#include <utility>
#include <typeinfo>
#include <type_traits>

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
    Handle(std::nullptr_t) : HandleBase() {}
    Handle(T* obj) :HandleBase(reinterpret_cast<Object*>(obj)) {}
    Handle() : HandleBase() {}
    Handle(const Handle& h) :HandleBase(h) {}
    Handle(Handle&& h) :HandleBase(std::move(h)) {}

    template<typename U, typename = typename std::enable_if<
                 std::is_base_of<T, U>::value &&
                 !std::is_same<T, U>::value
                 >::type>
    Handle(const Handle<U>& h) : HandleBase(h) {}

    template<typename U, typename = typename std::enable_if<
                 std::is_base_of<T, U>::value &&
                 !std::is_same<T, U>::value
                 >::type>
    Handle(Handle<U>&& h) : HandleBase(std::move(h)) {}

    void operator =(T* obj) {
        HandleBase::operator =(obj);
    }

    void operator =(const Handle& h) {
        HandleBase::operator =(h);
    }

    void operator =(Handle&& h) {
        HandleBase::operator =(std::move(h));
    }

    template<typename U, typename = typename std::enable_if<
                 std::is_base_of<T, U>::value &&
                 !std::is_same<T, U>::value
                 >::type>
    void operator =(const Handle<U>& h) {
        HandleBase::operator =(h);
    }

    template<typename U, typename = typename std::enable_if<
                 std::is_base_of<T, U>::value &&
                 !std::is_same<T, U>::value
                 >::type>
    void operator =(Handle<U>&& h) {
        HandleBase::operator =(std::move(h));
    }

    T* operator ->() const {
        return static_cast<T*>(Get());
    }

    explicit operator bool() const {
        return !!operator T*();
    }

    operator T*() const {
        return reinterpret_cast<T*>(Get());
    }

    template<typename = std::enable_if<!std::is_same<T, Object>::value>>
    explicit operator Object*() const {
        return Get();
    }

    template<typename D>
    explicit operator D*() const {
        return static_cast<D*>(operator T*());
    }

    template<typename U>
    Handle<U> CastTo() const {
        return static_cast<U*>(operator T*());
    }

    const std::type_info& TypeId() const {
        return typeid(*operator T*());
    }

    template<typename U>
    bool ExactInstanceOf() const {
        return TypeId() == typeid(U);
    }

    template<typename U>
    Handle<U> DynamicCastTo() const {
        return dynamic_cast<U*>(operator T*());
    }

    template<typename U>
    Handle<U> ExactCheckedCastTo() const {
        if (ExactInstanceOf<U>()) {
            return CastTo<U>();
        }
        return nullptr;
    }

    template<class S> friend class Handle;
};


}
}

#endif