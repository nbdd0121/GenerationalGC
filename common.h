#ifndef NORLIT_GC_COMMON_H
#define NORLIT_GC_COMMON_H

#include <cstdint>

namespace norlit {
namespace gc {

enum class Space : uint8_t {
    EDEN_SPACE,
    SURVIVOR_SPACE,
    TENURED_SPACE,
    LARGE_OBJECT_SPACE,
    STACK_SPACE
};

enum class Status : uint8_t {
    NOT_MARKED,
    MARKING,
    MARKED,
};

}
}

#endif