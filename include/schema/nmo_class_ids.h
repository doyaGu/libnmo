/**
 * @file nmo_class_ids.h
 * @brief Virtools class ID constants
 * 
 * Class IDs from official Virtools SDK (reference/include/CKDefines.h lines 308-383).
 * These IDs identify different object types in Virtools file format.
 */

#ifndef NMO_CLASS_IDS_H
#define NMO_CLASS_IDS_H

#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * CKOBJECT HIERARCHY CLASS IDS
 * ============================================================================= */

#define NMO_CID_OBJECT                  1
#define NMO_CID_PARAMETERIN             2
#define NMO_CID_PARAMETEROUT            3
#define NMO_CID_PARAMETEROPERATION      4
#define NMO_CID_STATE                   5
#define NMO_CID_BEHAVIORLINK            6
#define NMO_CID_BEHAVIOR                8
#define NMO_CID_BEHAVIORIO              9
#define NMO_CID_SCENE                   10
#define NMO_CID_SCENEOBJECT             11
#define NMO_CID_RENDERCONTEXT           12
#define NMO_CID_KINEMATICCHAIN          13
#define NMO_CID_OBJECTANIMATION         15
#define NMO_CID_ANIMATION               16
#define NMO_CID_KEYEDANIMATION          18
#define NMO_CID_BEOBJECT                19
#define NMO_CID_SYNCHRO                 20
#define NMO_CID_LEVEL                   21
#define NMO_CID_PLACE                   22
#define NMO_CID_GROUP                   23
#define NMO_CID_SOUND                   24
#define NMO_CID_WAVESOUND               25
#define NMO_CID_MIDISOUND               26
#define NMO_CID_2DENTITY                27
#define NMO_CID_SPRITE                  28
#define NMO_CID_SPRITETEXT              29
#define NMO_CID_MATERIAL                30
#define NMO_CID_TEXTURE                 31
#define NMO_CID_MESH                    32
#define NMO_CID_3DENTITY                33
#define NMO_CID_CAMERA                  34
#define NMO_CID_TARGETCAMERA            35
#define NMO_CID_CURVEPOINT              36
#define NMO_CID_SPRITE3D                37
#define NMO_CID_LIGHT                   38
#define NMO_CID_TARGETLIGHT             39
#define NMO_CID_CHARACTER               40
#define NMO_CID_3DOBJECT                41
#define NMO_CID_BODYPART                42
#define NMO_CID_CURVE                   43
#define NMO_CID_PARAMETERLOCAL          45
#define NMO_CID_PARAMETER               46
#define NMO_CID_RENDEROBJECT            47
#define NMO_CID_INTERFACEOBJECTMANAGER  48
#define NMO_CID_CRITICALSECTION         49
#define NMO_CID_GRID                    50
#define NMO_CID_LAYER                   51
#define NMO_CID_DATAARRAY               52
#define NMO_CID_PATCHMESH               53
#define NMO_CID_PROGRESSIVEMESH         54
#define NMO_CID_MAXCLASSID              55

/* =============================================================================
 * MANAGER CLASS IDS (Not CKObject derived)
 * ============================================================================= */

#define NMO_CID_OBJECTARRAY             80
#define NMO_CID_SCENEOBJECTDESC         81
#define NMO_CID_ATTRIBUTEMANAGER        82
#define NMO_CID_MESSAGEMANAGER          83
#define NMO_CID_COLLISIONMANAGER        84
#define NMO_CID_OBJECTMANAGER           85
#define NMO_CID_FLOORMANAGER            86
#define NMO_CID_RENDERMANAGER           87
#define NMO_CID_BEHAVIORMANAGER         88
#define NMO_CID_INPUTMANAGER            89
#define NMO_CID_PARAMETERMANAGER        90
#define NMO_CID_GRIDMANAGER             91
#define NMO_CID_SOUNDMANAGER            92
#define NMO_CID_TIMEMANAGER             93

#ifdef __cplusplus
}
#endif

#endif /* NMO_CLASS_IDS_H */
