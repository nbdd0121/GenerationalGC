#ifndef NORLIT_GC_PLATFORM_H
#define NORLIT_GC_PLATFORM_H

#include <cstddef>

namespace norlit {
namespace gc {

class Platform {
  public:
    static void* Allocate(size_t size);
    static void Free(void* ptr, size_t size);
};

}
}

#endif