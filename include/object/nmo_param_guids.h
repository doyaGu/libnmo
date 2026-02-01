/**
 * @file nmo_param_guids.h
 * @brief Virtools parameter GUID constants (subset)
 *
 * This header provides commonly used CKPGUID_* constants for
 * parameter metadata and schema migration logic.
 */

#ifndef NMO_PARAM_GUIDS_H
#define NMO_PARAM_GUIDS_H

#include "core/nmo_guid.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Scalar parameter types */
#define CKPGUID_INT        NMO_GUID(0x5a5716fd, 0x44e276d7)
#define CKPGUID_FLOAT      NMO_GUID(0x47884c3f, 0x432c2c20)
#define CKPGUID_BOOL       NMO_GUID(0x1ad52a8e, 0x5e741920)
#define CKPGUID_STRING     NMO_GUID(0x6bd010e2, 0x115617ea)
#define CKPGUID_KEY        NMO_GUID(0xfa6e1bdd, 0x62d2abd7)

/* Math parameter types */
#define CKPGUID_VECTOR     NMO_GUID(0x48824eae, 0x2fe47960)
#define CKPGUID_2DVECTOR   NMO_GUID(0x4efcb34a, 0x6079e42f)
#define CKPGUID_QUATERNION NMO_GUID(0x06c439ee, 0x45b50fc2)
#define CKPGUID_MATRIX     NMO_GUID(0x643f046e, 0x65211b71)
#define CKPGUID_COLOR      NMO_GUID(0x57d42fee, 0x7cbb3b91)
#define CKPGUID_BOX        NMO_GUID(0x668649c8, 0x283e2ee1)
#define CKPGUID_RECT       NMO_GUID(0x7ab20d20, 0x693044a9)

/* Object reference types */
#define CKPGUID_OBJECT     NMO_GUID(0x30ec20ab, 0x6df6517d)
#define CKPGUID_ID         NMO_GUID(0x71653557, 0x2d1b2e97)

/* Time types */
#define CKPGUID_TIME       NMO_GUID(0x54b4422b, 0x730f0f4f)
#define CKPGUID_OLDTIME    NMO_GUID(0x4a4d4867, 0x3c28773f)

#ifdef __cplusplus
}
#endif

#endif /* NMO_PARAM_GUIDS_H */
