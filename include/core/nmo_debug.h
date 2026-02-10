#ifndef NMO_DEBUG_H
#define NMO_DEBUG_H

#include <assert.h>

#ifdef NMO_ENABLE_DEBUG_ASSERTS
#define NMO_DEBUG_ASSERT(condition) assert(condition)
#else
#define NMO_DEBUG_ASSERT(condition) ((void)0)
#endif

#endif /* NMO_DEBUG_H */
