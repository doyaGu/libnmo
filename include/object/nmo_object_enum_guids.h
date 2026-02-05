/**
 * @file nmo_object_enum_guids.h
 * @brief GUIDs for CK/VX enums and flags used in reflection fields
 *
 * NOTE:
 * - Some GUIDs are explicitly overridden in enum_overrides.json.
 * - Others use the auto-generated GUIDs from nmo_type_generate_guid(name).
 */

#ifndef NMO_OBJECT_ENUM_GUIDS_H
#define NMO_OBJECT_ENUM_GUIDS_H

#include "core/nmo_guid.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Auto-generated GUIDs (nmo_type_generate_guid) */
#define NMO_GUID_FIELD_CK_OBJECT_FLAGS       {.d1 = 0xbeffcf75, .d2 = 0x7a6f7a77}
#define NMO_GUID_FIELD_CK_SCENE_FLAGS        {.d1 = 0x8833764d, .d2 = 0x973e6508}
#define NMO_GUID_FIELD_CK_2DENTITY_FLAGS     {.d1 = 0xefee90ae, .d2 = 0x8c2fe0b7}
#define NMO_GUID_FIELD_CK_3DENTITY_FLAGS     {.d1 = 0xefee90ae, .d2 = 0xa27dd9a9}
#define NMO_GUID_FIELD_VX_MOVEABLE_FLAGS     {.d1 = 0x901d7d23, .d2 = 0x18e2a2c5}
#define NMO_GUID_FIELD_VXMESH_FLAGS          {.d1 = 0xb4d819ba, .d2 = 0x694d1499}
#define NMO_GUID_FIELD_CK_BEHAVIOR_FLAGS     {.d1 = 0xca57328f, .d2 = 0x1edda205}
#define NMO_GUID_FIELD_CK_BEHAVIOR_TYPE      {.d1 = 0x90a8425a, .d2 = 0x1f0a9405}
#define NMO_GUID_FIELD_CK_TEXTURE_SAVEOPTIONS {.d1 = 0xb656be94, .d2 = 0x1f32ceaf}
#define NMO_GUID_FIELD_CK_SOUND_SAVEOPTIONS  {.d1 = 0xf2ac90ae, .d2 = 0xb47a9e0c}
#define NMO_GUID_FIELD_CK_WAVESOUND_STATE    {.d1 = 0xc24205a8, .d2 = 0xd005d9b2}

/* Explicit override GUIDs (enum_overrides.json) */
#define NMO_GUID_FIELD_CK_SCENEOBJECTACTIVITY_FLAGS {.d1 = 0x1cd24241, .d2 = 0x533c0f8f}
#define NMO_GUID_FIELD_VXSPRITE3D_TYPE              {.d1 = 0x1d1a3403, .d2 = 0x535e7252}
#define NMO_GUID_FIELD_VX_PIXELFORMAT               {.d1 = 0x79465229, .d2 = 0x61af2af1}
#define NMO_GUID_FIELD_VXFOG_MODE                   {.d1 = 0x686f28ef, .d2 = 0x5ecc7766}

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_ENUM_GUIDS_H */
