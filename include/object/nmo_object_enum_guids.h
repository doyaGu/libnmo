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
#include "object/nmo_param_guids.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Auto-generated GUIDs (nmo_type_generate_guid) */
#define NMO_GUID_FIELD_CK_OBJECT_FLAGS NMO_GUID(0xbeffcf75, 0x7a6f7a77)
#define NMO_GUID_FIELD_CK_SCENE_FLAGS NMO_GUID(0x8833764d, 0x973e6508)
#define NMO_GUID_FIELD_CK_2DENTITY_FLAGS NMO_GUID(0xefee90ae, 0x8c2fe0b7)
#define NMO_GUID_FIELD_CK_3DENTITY_FLAGS NMO_GUID(0xefee90ae, 0xa27dd9a9)
#define NMO_GUID_FIELD_VX_MOVEABLE_FLAGS NMO_GUID(0x901d7d23, 0x18e2a2c5)
#define NMO_GUID_FIELD_VXMESH_FLAGS NMO_GUID(0xb4d819ba, 0x694d1499)
#define NMO_GUID_FIELD_CK_BEHAVIOR_FLAGS NMO_GUID(0xca57328f, 0x1edda205)
#define NMO_GUID_FIELD_CK_BEHAVIOR_TYPE NMO_GUID(0x90a8425a, 0x1f0a9405)
#define NMO_GUID_FIELD_CK_TEXTURE_SAVEOPTIONS NMO_GUID(0xb656be94, 0x1f32ceaf)
#define NMO_GUID_FIELD_CK_SOUND_SAVEOPTIONS NMO_GUID(0xf2ac90ae, 0xb47a9e0c)
#define NMO_GUID_FIELD_CK_WAVESOUND_STATE NMO_GUID(0xc24205a8, 0xd005d9b2)
/* Explicit override GUIDs (enum_overrides.json) */
#define NMO_GUID_FIELD_CK_SCENEOBJECTACTIVITY_FLAGS CKPGUID_SCENEACTIVITYFLAGS
#define NMO_GUID_FIELD_VXSPRITE3D_TYPE              CKPGUID_3DSPRITEMODE
#define NMO_GUID_FIELD_VX_PIXELFORMAT               CKPGUID_PIXELFORMAT
#define NMO_GUID_FIELD_VXFOG_MODE                   CKPGUID_FOGMODE

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_ENUM_GUIDS_H */
