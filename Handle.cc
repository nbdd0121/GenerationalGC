#include "Handle.h"
#include "Platform.h"
#include <bitset>
#include <cassert>

namespace norlit {
namespace gc {
namespace detail {
class HandleGroup;
}
}
}

using namespace norlit::gc;
using namespace norlit::gc::detail;

class norlit::gc::detail::HandleGroup : public Object {
  private:
    static const size_t kHandlesPerGroup = 984;

    // Tracks allocation using a bitmap_. TODO: Use a custom class to accelerate the allocation
    std::bitset<kHandlesPerGroup> bitmap_;
    HandleGroup* next_ = nullptr;
    size_t size_ = 0;
    Object* handles_[kHandlesPerGroup];

  protected:
    virtual void IterateField(const FieldIterator& iter) override;

  public:
    HandleGroup();

    Object** Allocate();
    void Free(Object** ptr);
    void Write(Object** ptr, Object* data) {
        // HandleGroup is always in STACK_SPACE
        // We specially design Handle, so pointing to stack space is allowed
        // On stack objects may be regard as tagged pointers but we detail them in the same way
        if (data && !data->IsTagged() && data->space_ != Space::STACK_SPACE) {
            data->IncRefCount();
        }
        if (*ptr && !(*ptr)->IsTagged() && (*ptr)->space_ != Space::STACK_SPACE) {
            (*ptr)->DecRefCount();
        }
        *ptr = data;
    }

    // HandleRoot is intended to serve as root.
    // Therefore, it should not be managed by heap
    void* operator new(size_t size) {
        return Platform::Allocate(size);
    }

    void operator delete(void* ptr) {
        Platform::Free(ptr, sizeof(HandleGroup));
    }
};

namespace {
HandleGroup* root = nullptr;
}

void HandleGroup::IterateField(const FieldIterator& iter) {
    if (size_) {
        for (size_t i = 0; i < kHandlesPerGroup; i++) {
            Object* data = handles_[i];
            // Special treatment to make handle to stack object legal
            if (data && !data->IsTagged() && data->space_ != Space::STACK_SPACE) {
                iter(&handles_[i]);
            }
        }
    }
}

HandleGroup::HandleGroup() {
    for (size_t i = 0; i < kHandlesPerGroup; i++) {
        handles_[i] = nullptr;
    }
}

Object** HandleGroup::Allocate() {
    for (size_t i = 0; i < kHandlesPerGroup; i++) {
        if (!bitmap_.test(i)) {
            bitmap_.set(i);
            size_++;
            return &handles_[i];
        }
    }
    if (!next_) {
        next_ = new HandleGroup();
    }
    return next_->Allocate();
}

void HandleGroup::Free(Object** ptr) {
    ptrdiff_t offset = ptr - handles_;
    if (offset >= 0 && offset < static_cast<int>(kHandlesPerGroup)) {
        Write(ptr, nullptr);
        bitmap_.reset(offset);
        size_--;
    } else {
        assert(next_);
        next_->Free(ptr);
        if (!next_->size_) {
            HandleGroup* temp = next_;
            next_ = temp->next_;
            temp->next_ = nullptr;
            delete temp;
        }
    }
}

HandleBase::HandleBase() {
    if (!root) {
        root = new HandleGroup();
    }
    object_ = nullptr;
}

HandleBase::HandleBase(Object* obj) {
    if (!root) {
        root = new HandleGroup();
    }
    object_ = root->Allocate();
    root->Write(object_, obj);
}

HandleBase::HandleBase(const HandleBase& obj) {
    // No need to check root!=nullptr.
    // Copy-ctor means HandleBase is at least constructed once before
    object_ = root->Allocate();
    root->Write(object_, *obj.object_);
}

HandleBase::HandleBase(HandleBase&& obj) {
    object_ = obj.object_;
    // Destructor will take care of this
    obj.object_ = nullptr;
}

void HandleBase::operator= (Object* obj) {
    if (!object_) {
        object_ = root->Allocate();
    }
    root->Write(object_, obj);
}

void HandleBase::operator = (const HandleBase& obj) {
    operator=(obj.operator*());
}

void HandleBase::operator = (HandleBase&& obj) {
    std::swap(object_, obj.object_);
}

HandleBase::~HandleBase() {
    if (object_) {
        root->Free(object_);
    }
}