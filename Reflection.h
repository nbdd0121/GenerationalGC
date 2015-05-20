#ifndef NORLIT_GC_REFLECTION_H
#define NORLIT_GC_REFLECTION_H

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

template<typename T>
class Reflection;

template<>
class Reflection<Object> {
  public:
    // Iterate fields for a given object
    virtual void IterateField(Object*, const FieldIterator&) {}
};

// Base class for all reflections
template<typename T>
class Reflection: public Reflection<Object> {
  public:
    virtual void IterateField(Object* ptr, const FieldIterator& iter) override {
        IterateField((T*)ptr, iter);
    }
    virtual void IterateField(T*, const FieldIterator&) {}
};

}
}

#endif