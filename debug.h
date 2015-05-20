#ifndef NORLIT_GC_DEBUG_H
#define NORLIT_GC_DEBUG_H

#define NORLIT_DEBUG_MODE 2

#if NORLIT_DEBUG_MODE == 0
#define NDEBUG 1
#define debug(...)
#elif NORLIT_DEBUG_MODE == 1
#define debug(...)
#elif NORLIT_DEBUG_MODE == 2
#define debug(...) printf(__VA_ARGS__)
#endif

#include <cassert>

#endif