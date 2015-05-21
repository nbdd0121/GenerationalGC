Generation GC
=====
This is a project aiming at providing a convenient garbage collection framework for C++.

##Space Division

Name              | Description
----              | -----------
Eden Space        | Newly created objects. Objects survived a gc will be moved to survivor space.
Survivor Space    | Divided mark-copy space. Objects that survives few gc will be kept in this space. Once object survives certain gc cycles, it will be promoted to the tenured space.
Tenured Space     | Area that will not be marked in minor GC. Write barrier is used, and in current implementation, a reference counting mechanism is used to speed up the marking phase. Mark-compact will take in place when a major GC happens.
Large Object Space| Large objects will be allocated in this space. This space is similar to tenured space, but mark-sweep instead of mark-compact is used.
Stack Space       | Non-heap objects. In this GC design, objects can be allocated on stack instead of on heap. They act as GC roots, but they should **not** be referenced by any other objects.

##Important Notice
When using this GC, make sure that you hold the handle instead of pointer to a object, because objects may be moved and thus pointer will be invalidated. Consider the following code.
```C++
handle->Method(new Object());
```
When a gc started during `new Object()`,  the pointer returned by handle.operator ->() can be invalidated. Therefore, you should write the code as:
```C++
Object* arg0 = new Object(); // or use handle
handle->Method(arg0);
```

> Note:
> In most compilers the wrong sample works (these compilers sequence evaluation of arguments before evaluation of this argument).
> But the C++ specification states the evaluation as unsequenced, so it is undefined behavior and may cause problems.

In method using `this`, make sure that you did not new Object.
Such rules apply to new, which returns `this`; therefore, you should never create an object in constructor.
If you need to do so, one way is to use a helper method (`static T* New();` for example) to do that, or you can use NoGC to prevent GC from happening.

You **SHOULD** use static methods that take `const Handle&` if the method contains actions that might trigger GC, such as new operation.

**DO NOT USE** multi-inheritance. The class data layout can be unexpected, causing errors.

When you use tagged pointers, make sure you are **NOT** calling virtual functions on tagged pointers.

When objects are moved in heap, **NO** copy-ctor or any kind of notification will be made.

##API Reference
- All GC objects are **REQUIRED** to inherit from `norlit::gc::Object`.
- To allocate a object on gc heap, simply use new operator.
- When writing to a GC-managed pointer, do not use assignment. Instead, use `WriteBarrier(&field, data)` in replace of `field = data`; This is essential since Tenured Space, Large Object Space and Stack Space use reference counting mechanism.
- Override `virtual void IterateField(const norlit::gc::FieldIterator&)` and call the iterator with pointer to each managed pointer in the class.
- Use `norlit::gc::Heap::MinorGC()` or `norlit::gc::Heap::MajorGC()` to trigger garbage collection.
- Use `norlit::gc::Handle` to manage reference on heap instead of pointers.
- All allocated heap objects are guaranteed to align on 8 bytes. Tagged pointers are allowed and will not be considered in GC.
- Use `norlit::gc::Array<T>` for an array of references. Use `norlit::gc::ValueArray<T>` for an array of non-gc-managed values (such as POD types).
- Use `norlit::gc::NoGC` to prevent GC from happening. As long as a NoGC instance is alive, GC will not be triggered, and manually triggered GC will cause an exception. When Eden Space is full and GC cannot trigger, new small objects will be created directly on Survivor Space.

##Currently Problems
 - Marking is inefficient. Currently there is no queue implemented, so a walk through all objects for several times is needed.
 - This is single threaded. This is probably not going to change since the author has no demand for multi-threading, and cost for maintaining thread synchronization is high. A stop-the-world is needed which cannot be written in a portable way.
