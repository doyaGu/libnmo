#ifndef NMO_OWNERSHIP_H
#define NMO_OWNERSHIP_H

#include "nmo_types.h"
#include "core/nmo_debug.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nmo_ownership_tag {
    NMO_OWNERSHIP_UNKNOWN = 0,
    NMO_OWNERSHIP_ARENA = 1,
    NMO_OWNERSHIP_HEAP = 2,
    NMO_OWNERSHIP_EXTERNAL = 3
} nmo_ownership_tag_t;

NMO_API bool nmo_ownership_checks_enabled(void);
NMO_API void nmo_ownership_set_checks_enabled(bool enabled);
NMO_API void nmo_ownership_reload_checks_from_env(void);

#ifdef NMO_ENABLE_DEBUG_ASSERTS
#define NMO_OWNERSHIP_EXPECT(value, expected) \
    do { \
        if (nmo_ownership_checks_enabled()) { \
            NMO_DEBUG_ASSERT((value) == (expected)); \
        } \
    } while (0)
#define NMO_OWNERSHIP_ASSERT_VALID(value) \
    do { \
        if (nmo_ownership_checks_enabled()) { \
            NMO_DEBUG_ASSERT((value) == NMO_OWNERSHIP_ARENA || \
                             (value) == NMO_OWNERSHIP_HEAP || \
                             (value) == NMO_OWNERSHIP_EXTERNAL || \
                             (value) == NMO_OWNERSHIP_UNKNOWN); \
        } \
    } while (0)
#else
#define NMO_OWNERSHIP_EXPECT(value, expected) ((void)0)
#define NMO_OWNERSHIP_ASSERT_VALID(value) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* NMO_OWNERSHIP_H */
