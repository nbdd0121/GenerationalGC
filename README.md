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

You **SHOULD** use static methods that take `const Handle&` if the method contains actions that might trigger GC, such as new operation.

**DO NOT USE** multi-inheritance. The class data layout can be unexpected, causing errors.

When you use tagged pointers, make sure you are **NOT** calling virtual functions on tagged pointers.

##API Reference
- All GC objects are required to inherit from `norlit::gc::Object`
- When writing to a GC-managed pointer, do not use assignment. Instead, use `WriteBarrier(&field, data)` in replace of `field = data`; This is essential since Tenured Space, Large Object Space and Stack Space use reference counting mechanism.
- `norlit::gc::Object` has a constructor take argument of `norlit::gc::Reflection`. You should create a class inherit from Reflection&lt;T> and  override `virtual void IterateField(T*, const norlit::gc::FieldIterator&)`. As a special case, if you do not have any field, you may also pass `nullptr` as a argument.
- Use `norlit::gc::Heap::MinorGC()` or `norlit::gc::Heap::MajorGC()` to trigger garbage collection.
- Use `norlit::gc::Handle` to manage reference on heap instead of pointers.

##Currently Problems
 - The class Handle is a stub. Currently it is implemented by a on-stack object, which is super inefficient. It will soon be changed in the future.
 - Marking is inefficient. Currently there is no queue implemented, so a walk through all objects for several times is needed.
 - This is single threaded. This is probably not going to change since the author has no demand for multi-threading, and cost for maintaining thread synchronization is high. A stop-the-world is needed which cannot be written in a portable way.

##TODOs
- Refine Handle
- Allow GC to be temporary disabled when performing certain task