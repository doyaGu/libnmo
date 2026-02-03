/**
 * @file nmo_ckstatesave_ids.h
 * @brief Virtools CKStateChunk identifier/flag constants (ported from CK2's CKDefines2.h)
 *
 * These constants are used as identifiers inside CKStateChunk payloads produced by
 * CKSaveObjectState / CKFile save paths. In Virtools SDK they are also described as
 * "state save flags" (bitmasks) passed to CKSaveObjectState; in practice, many
 * schemas use them as identifier tags when reading/writing optional sections.
 *
 * This header is a value-only port of CK2/include/CKDefines2.h for use by libnmo
 * schemas. Keep values identical to the SDK for interoperability.
 */

#ifndef NMO_CKSTATESAVE_IDS_H
#define NMO_CKSTATESAVE_IDS_H

#include "nmo_types.h"

/* ============================================================================
 * Global
 * ============================================================================ */

#define CK_STATESAVE_ALL 0xFFFFFFFFu

/* ============================================================================
 * Object
 * ============================================================================ */

#define CK_STATESAVE_NAME              0x00000001u /* Obsolete */
#define CK_STATESAVE_ID                0x00000002u /* Obsolete */
#define CK_STATESAVE_OBJECTHIDDEN      0x00000004u /* The object is hidden */
#define CK_STATESAVE_OBJECTHIERAHIDDEN 0x00000018u /* The object is hidden hierarchically */
#define CK_STATESAVE_OBJECTALL         0x0000000Fu

/* ============================================================================
 * BeObject
 * ============================================================================ */

#define CK_STATESAVE_ATTRIBUTES     0x00000010u /* Obsolete */
#define CK_STATESAVE_NEWATTRIBUTES  0x00000011u /* Save Attributes */
#define CK_STATESAVE_GROUPS         0x00000020u /* Obsolete */
#define CK_STATESAVE_DATAS          0x00000040u /* Save Flags and (Waiting for message) status */
#define CK_STATESAVE_SOUNDS         0x00000080u /* Obsolete */
#define CK_STATESAVE_BEHAVIORS      0x00000100u /* Obsolete */
#define CK_STATESAVE_PARAMETERS     0x00000200u /* Obsolete */
#define CK_STATESAVE_SINGLEACTIVITY 0x00000400u /* SINGLE ACTIVITY */
#define CK_STATESAVE_SCRIPTS        0x00000800u /* Obsolete */
#define CK_STATESAVE_BEOBJECTONLY   0x00000FF0u /* Save only BeObject specific data */
#define CK_STATESAVE_BEOBJECTALL    0x00000FFFu /* Save All data */

/* ============================================================================
 * 3dEntity
 * ============================================================================ */

#define CK_STATESAVE_3DENTITYSKINDATANORMALS 0x00001000u /* Save Skin normals */
#define CK_STATESAVE_ANIMATION               0x00002000u /* Obsolete */
#define CK_STATESAVE_MESHS                   0x00004000u /* Save List of mesh */
#define CK_STATESAVE_PARENT                  0x00008000u /* Save Parent */
#define CK_STATESAVE_3DENTITYFLAGS           0x00010000u /* Save Flags */
#define CK_STATESAVE_3DENTITYMATRIX          0x00020000u /* Save Position/orientation/Scale */
#define CK_STATESAVE_3DENTITYHIERARCHY       0x00040000u /* Obsolete */
#define CK_STATESAVE_3DENTITYPLACE           0x00080000u /* Save Place in which the Entity is referenced */
#define CK_STATESAVE_3DENTITYNDATA           0x00100000u /* Reserved for future use */
#define CK_STATESAVE_3DENTITYSKINDATA        0x00200000u /* Save Skin data */
#define CK_STATESAVE_3DENTITYONLY            0x003FF000u /* Save only 3dEntity specific data */
#define CK_STATESAVE_3DENTITYALL             0x003FFFFFu /* Save All data for sub-classes */

/* ============================================================================
 * Light (+ Target Light)
 * ============================================================================ */

#define CK_STATESAVE_LIGHTDATA       0x00400000u
#define CK_STATESAVE_LIGHTDATA2      0x00800000u
#define CK_STATESAVE_LIGHTRESERVED1  0x01000000u
#define CK_STATESAVE_LIGHTRESERVED2  0x02000000u
#define CK_STATESAVE_LIGHTRESERVED3  0x04000000u
#define CK_STATESAVE_LIGHTRESERVED4  0x08000000u
#define CK_STATESAVE_LIGHTONLY       0x0FC00000u
#define CK_STATESAVE_LIGHTALL        0x0FFFFFFFu

#define CK_STATESAVE_TLIGHTTARGET    0x80000000u
#define CK_STATESAVE_TLIGHTRESERVED0 0x10000000u
#define CK_STATESAVE_TLIGHTRESERVED1 0x20000000u
#define CK_STATESAVE_TLIGHTRESERVED2 0x40000000u
#define CK_STATESAVE_TLIGHTONLY      0xF0000000u
#define CK_STATESAVE_TLIGHTALL       0xFFFFFFFFu

/* ============================================================================
 * Camera (+ Target Camera)
 * ============================================================================ */

#define CK_STATESAVE_CAMERAFOV        0x00400000u
#define CK_STATESAVE_CAMERAPROJTYPE   0x00800000u
#define CK_STATESAVE_CAMERAOTHOZOOM   0x01000000u
#define CK_STATESAVE_CAMERAASPECT     0x02000000u
#define CK_STATESAVE_CAMERAPLANES     0x04000000u
#define CK_STATESAVE_CAMERARESERVED2  0x08000000u
#define CK_STATESAVE_CAMERAONLY       0x0FC00000u
#define CK_STATESAVE_CAMERAALL        0x0FFFFFFFu

#define CK_STATESAVE_TCAMERATARGET    0x10000000u
#define CK_STATESAVE_TCAMERARESERVED1 0x20000000u
#define CK_STATESAVE_TCAMERARESERVED2 0x40000000u
#define CK_STATESAVE_TCAMERAONLY      0x70000000u
#define CK_STATESAVE_TCAMERAALL       0x7FFFFFFFu

/* ============================================================================
 * Sprite3D
 * ============================================================================ */

#define CK_STATESAVE_SPRITE3DDATA      0x00400000u
#define CK_STATESAVE_SPRITE3DRESERVED0 0x00800000u
#define CK_STATESAVE_SPRITE3DRESERVED1 0x01000000u
#define CK_STATESAVE_SPRITE3DRESERVED2 0x02000000u
#define CK_STATESAVE_SPRITE3DRESERVED3 0x04000000u
#define CK_STATESAVE_SPRITE3DRESERVED4 0x08000000u
#define CK_STATESAVE_SPRITE3DONLY      0x0FC00000u
#define CK_STATESAVE_SPRITE3DALL       0x0FFFFFFFu

/* ============================================================================
 * 3dObject
 * ============================================================================ */

#define CK_STATESAVE_3DOBJECTATTRIBUTES 0x00400000u /* Obsolete */
#define CK_STATESAVE_3DOBJECTRESERVED   0x00800000u
#define CK_STATESAVE_3DOBJECTRONLY      0x00C00000u
#define CK_STATESAVE_3DOBJECTALL        0x03FFFFFFu

/* ============================================================================
 * BodyPart
 * ============================================================================ */

#define CK_STATESAVE_BODYPARTROTJOINT  0x01000000u
#define CK_STATESAVE_BODYPARTPOSJOINT  0x02000000u
#define CK_STATESAVE_BODYPARTCHARACTER 0x04000000u
#define CK_STATESAVE_BODYPARTRESERVED1 0x08000000u
#define CK_STATESAVE_BODYPARTRESERVED2 0x10000000u
#define CK_STATESAVE_BODYPARTRESERVED3 0x20000000u
#define CK_STATESAVE_BODYPARTRESERVED4 0x40000000u
#define CK_STATESAVE_BODYPARTONLY      0x7F000000u
#define CK_STATESAVE_BODYPARTALL       0x7FFFFFFFu

/* ============================================================================
 * Character
 * ============================================================================ */

#define CK_STATESAVE_CHARACTERBODYPARTS  0x00400000u /* Obsolete */
#define CK_STATESAVE_CHARACTERKINECHAINS 0x00800000u /* Obsolete */
#define CK_STATESAVE_CHARACTERANIMATIONS 0x01000000u /* Obsolete */
#define CK_STATESAVE_CHARACTERROOT       0x02000000u /* Obsolete */
#define CK_STATESAVE_CHARACTERSAVEANIMS  0x04000000u
#define CK_STATESAVE_CHARACTERSAVECHAINS 0x08000000u /* Obsolete */
#define CK_STATESAVE_CHARACTERSAVEPARTS  0x10000000u
#define CK_STATESAVE_CHARACTERFLOORREF   0x20000000u
#define CK_STATESAVE_CHARACTERRESERVED2  0x40000000u
#define CK_STATESAVE_CHARACTERRESERVED3  0x80000000u
#define CK_STATESAVE_CHARACTERONLY       0xFFC00000u
#define CK_STATESAVE_CHARACTERALL        0xFFFFFFFFu

/* ============================================================================
 * Curve (+ Curve Point)
 * ============================================================================ */

#define CK_STATESAVE_CURVEFITCOEFF         0x00400000u
#define CK_STATESAVE_CURVECONTROLPOINT     0x00800000u
#define CK_STATESAVE_CURVESTEPS            0x01000000u
#define CK_STATESAVE_CURVEOPEN             0x02000000u
#define CK_STATESAVE_CURVERESERVED1        0x04000000u
#define CK_STATESAVE_CURVERESERVED2        0x08000000u

#define CK_STATESAVE_CURVEPOINTDEFAULTDATA 0x10000000u
#define CK_STATESAVE_CURVEPOINTTCB         0x20000000u
#define CK_STATESAVE_CURVEPOINTTANGENTS    0x40000000u
#define CK_STATESAVE_CURVEPOINTCURVEPOS    0x80000000u
#define CK_STATESAVE_CURVESAVEPOINTS       0xFF000000u

#define CK_STATESAVE_CURVEONLY             0xFFC00000u
#define CK_STATESAVE_CURVEALL              0xFFFFFFFFu

/* ============================================================================
 * 2dEntity
 * ============================================================================ */

#define CK_STATESAVE_2DENTITYSRCSIZE   0x00001000u
#define CK_STATESAVE_2DENTITYSIZE      0x00002000u
#define CK_STATESAVE_2DENTITYFLAGS     0x00004000u
#define CK_STATESAVE_2DENTITYPOS       0x00008000u
#define CK_STATESAVE_2DENTITYZORDER    0x00100000u
#define CK_STATESAVE_2DENTITYONLY      0x0010F000u
#define CK_STATESAVE_2DENTITYMATERIAL  0x00200000u
#define CK_STATESAVE_2DENTITYHIERARCHY 0x00400000u
#define CK_STATESAVE_2DENTITYALL       0x0070FFFFu

/* ============================================================================
 * Sprite
 * ============================================================================ */

#define CK_STATESAVE_SPRITECURRENTIMAGE  0x00010000u
#define CK_STATESAVE_SPRITETRANSPARENT   0x00020000u
#define CK_STATESAVE_SPRITEBITMAPS       0x00040000u /* Obsolete */
#define CK_STATESAVE_SPRITESHARED        0x00080000u
#define CK_STATESAVE_SPRITEDONOTUSE      0x00100000u
#define CK_STATESAVE_SPRITEAVIFILENAME   0x00200000u /* Obsolete */
#define CK_STATESAVE_SPRITEFILENAMES     0x00400000u /* Obsolete */
#define CK_STATESAVE_SPRITECOMPRESSED    0x00800000u /* Obsolete */
#define CK_STATESAVE_SPRITEREADER        0x10000000u
#define CK_STATESAVE_SPRITEFORMAT        0x20000000u
#define CK_STATESAVE_SPRITEVIDEOFORMAT   0x40000000u
#define CK_STATESAVE_SPRITESYSTEMCACHING 0x80000000u
#define CK_STATESAVE_SPRITERENDEROPTIONS 0x80800000u
#define CK_STATESAVE_SPRITEONLY          0xF0EF0000u
#define CK_STATESAVE_SPRITEALL           0x70FFFFFFu

/* ============================================================================
 * SpriteText
 * ============================================================================ */

#define CK_STATESAVE_SPRITETEXT           0x01000000u
#define CK_STATESAVE_SPRITEFONT           0x02000000u
#define CK_STATESAVE_SPRITETEXTCOLOR      0x04000000u
#define CK_STATESAVE_SPRITETEXTRESERVED   0x08000000u
#define CK_STATESAVE_SPRITETEXTDOTNOTUSE  0x10000000u
#define CK_STATESAVE_SPRITETEXTDONOTUSED2 0x20000000u
#define CK_STATESAVE_SPRITETEXTONLY       0x0F000000u
#define CK_STATESAVE_SPRITETEXTALL        0x3FFFFFFFu

/* ============================================================================
 * Sound
 * ============================================================================ */

#define CK_STATESAVE_SOUNDFILENAME  0x00001000u
#define CK_STATESAVE_SOUNDRESERVED1 0x00002000u
#define CK_STATESAVE_SOUNDRESERVED2 0x00004000u
#define CK_STATESAVE_SOUNDRESERVED3 0x00008000u
#define CK_STATESAVE_SOUNDRESERVED4 0x00010000u
#define CK_STATESAVE_SOUNDRESERVED5 0x00020000u
#define CK_STATESAVE_SOUNDRESERVED6 0x00040000u
#define CK_STATESAVE_SOUNDRESERVED7 0x00080000u
#define CK_STATESAVE_SOUNDONLY      0x000FF000u
#define CK_STATESAVE_SOUNDALL       0x000FFFFFu

/* ============================================================================
 * Wave Sound
 * ============================================================================ */

#define CK_STATESAVE_WAVSOUNDFILE      0x00100000u
#define CK_STATESAVE_WAVSOUNDDATA      0x00200000u /* Obsolete */
#define CK_STATESAVE_WAVSOUNDDATA2     0x00400000u
#define CK_STATESAVE_WAVSOUNDDURATION  0x00800000u
#define CK_STATESAVE_WAVSOUNDRESERVED4 0x01000000u
#define CK_STATESAVE_WAVSOUNDRESERVED5 0x02000000u
#define CK_STATESAVE_WAVSOUNDRESERVED6 0x04000000u
#define CK_STATESAVE_WAVSOUNDRESERVED7 0x08000000u
#define CK_STATESAVE_WAVSOUNDONLY      0x0FF00000u
#define CK_STATESAVE_WAVSOUNDALL       0x0FFFFFFFu

/* ============================================================================
 * Midi Sound
 * ============================================================================ */

#define CK_STATESAVE_MIDISOUNDFILE      0x00100000u
#define CK_STATESAVE_MIDISOUNDDATA      0x00200000u
#define CK_STATESAVE_MIDISOUNDRESERVED2 0x00400000u
#define CK_STATESAVE_MIDISOUNDRESERVED3 0x00800000u
#define CK_STATESAVE_MIDISOUNDRESERVED4 0x01000000u
#define CK_STATESAVE_MIDISOUNDRESERVED5 0x02000000u
#define CK_STATESAVE_MIDISOUNDRESERVED6 0x04000000u
#define CK_STATESAVE_MIDISOUNDRESERVED7 0x08000000u
#define CK_STATESAVE_MIDISOUNDONLY      0x0FF00000u
#define CK_STATESAVE_MIDISOUNDALL       0x0FFFFFFFu

/* ============================================================================
 * Place
 * ============================================================================ */

#define CK_STATESAVE_PLACEPORTALS    0x00001000u
#define CK_STATESAVE_PLACECAMERA     0x00002000u
#define CK_STATESAVE_PLACEREFERENCES 0x00004000u
#define CK_STATESAVE_PLACELEVEL      0x00008000u
#define CK_STATESAVE_PLACEALL        0x0000FFFFu

/* ============================================================================
 * Level
 * ============================================================================ */

#define CK_STATESAVE_LEVELRESERVED0    0x00001000u
#define CK_STATESAVE_LEVELINACTIVEMAN  0x00002000u
#define CK_STATESAVE_LEVELDUPLICATEMAN 0x00004000u
#define CK_STATESAVE_LEVELDEFAULTDATA  0x20000000u
#define CK_STATESAVE_LEVELSCENE        0x80000000u
#define CK_STATESAVE_LEVELALL          0xFFFFFFFFu

/* ============================================================================
 * Group
 * ============================================================================ */

#define CK_STATESAVE_GROUPDATA      0x00001000u
#define CK_STATESAVE_GROUPRESERVED1 0x00002000u
#define CK_STATESAVE_GROUPRESERVED2 0x00004000u
#define CK_STATESAVE_GROUPRESERVED3 0x00008000u
#define CK_STATESAVE_GROUPRESERVED4 0x00010000u
#define CK_STATESAVE_GROUPRESERVED5 0x00020000u
#define CK_STATESAVE_GROUPRESERVED6 0x00040000u
#define CK_STATESAVE_GROUPRESERVED7 0x00080000u
#define CK_STATESAVE_GROUPALL       0x000FFFFFu

/* ============================================================================
 * Mesh
 * ============================================================================ */

#define CK_STATESAVE_MESHRESERVED0    0x00001000u
#define CK_STATESAVE_MESHFLAGS        0x00002000u
#define CK_STATESAVE_MESHCHANNELS     0x00004000u
#define CK_STATESAVE_MESHFACECHANMASK 0x00008000u
#define CK_STATESAVE_MESHFACES        0x00010000u
#define CK_STATESAVE_MESHVERTICES     0x00020000u
#define CK_STATESAVE_MESHLINES        0x00040000u
#define CK_STATESAVE_MESHWEIGHTS      0x00080000u
#define CK_STATESAVE_MESHMATERIALS    0x00100000u
#define CK_STATESAVE_MESHRESERVED1    0x00200000u
#define CK_STATESAVE_MESHRESERVED2    0x00400000u
#define CK_STATESAVE_PROGRESSIVEMESH  0x00800000u
#define CK_STATESAVE_MESHONLY         0x00FFF000u
#define CK_STATESAVE_MESHALL          0x00FFFFFFu

/* ============================================================================
 * Patch Mesh
 * ============================================================================ */

#define CK_STATESAVE_PATCHMESHDATA      0x00800000u /* Obsolete */
#define CK_STATESAVE_PATCHMESHDATA2     0x01000000u /* Obsolete */
#define CK_STATESAVE_PATCHMESHSMOOTH    0x02000000u /* Obsolete */
#define CK_STATESAVE_PATCHMESHMATERIALS 0x04000000u /* Obsolete */
#define CK_STATESAVE_PATCHMESHDATA3     0x08000000u /* Save Patch Data */
#define CK_STATESAVE_PATCHMESHONLY      0x0FF00000u
#define CK_STATESAVE_PATCHMESHALL       0x0FFFFFFFu

/* ============================================================================
 * Material
 * ============================================================================ */

#define CK_STATESAVE_MATDATA      0x00001000u
#define CK_STATESAVE_MATDATA2     0x00002000u
#define CK_STATESAVE_MATDATA3     0x00004000u
#define CK_STATESAVE_MATDATA4     0x00008000u
#define CK_STATESAVE_MATDATA5     0x00010000u
#define CK_STATESAVE_MATRESERVED5 0x00020000u
#define CK_STATESAVE_MATRESERVED6 0x00040000u
#define CK_STATESAVE_MATRESERVED7 0x00080000u
#define CK_STATESAVE_MATERIALONLY 0x000FF000u
#define CK_STATESAVE_MATERIALALL  0x0FFFFFFFu

/* ============================================================================
 * Texture
 * ============================================================================ */

#define CK_STATESAVE_TEXAVIFILENAME   0x00001000u
#define CK_STATESAVE_TEXCURRENTIMAGE  0x00002000u
#define CK_STATESAVE_TEXBITMAPS       0x00004000u /* Obsolete */
#define CK_STATESAVE_TEXTRANSPARENT   0x00008000u
#define CK_STATESAVE_TEXFILENAMES     0x00010000u
#define CK_STATESAVE_TEXCOMPRESSED    0x00020000u
#define CK_STATESAVE_TEXVIDEOFORMAT   0x00040000u
#define CK_STATESAVE_TEXSAVEFORMAT    0x00080000u
#define CK_STATESAVE_TEXREADER        0x00100000u
#define CK_STATESAVE_PICKTHRESHOLD    0x00200000u
#define CK_STATESAVE_USERMIPMAP       0x00400000u
#define CK_STATESAVE_TEXSYSTEMCACHING 0x00800000u
#define CK_STATESAVE_OLDTEXONLY       0x002FF000u
#define CK_STATESAVE_TEXONLY          0x00FFF000u
#define CK_STATESAVE_TEXALL           0x002FFFFFu

/* ============================================================================
 * 2d Curve
 * ============================================================================ */

#define CK_STATESAVE_2DCURVERESERVED0        0x00000010u
#define CK_STATESAVE_2DCURVERESERVED4        0x00000020u
#define CK_STATESAVE_2DCURVEFITCOEFF         0x00000040u
#define CK_STATESAVE_2DCURVECONTROLPOINT     0x00000080u
#define CK_STATESAVE_2DCURVENEWDATA          0x00000100u
#define CK_STATESAVE_2DCURVERESERVED2        0x00000200u
#define CK_STATESAVE_2DCURVERESERVED3        0x00000400u
#define CK_STATESAVE_2DCURVEPOINTTCB         0x00000800u
#define CK_STATESAVE_2DCURVEPOINTTANGENTS    0x00001000u
#define CK_STATESAVE_2DCURVEPOINT2DCURVEPOS  0x00002000u
#define CK_STATESAVE_2DCURVEPOINTDEFAULTDATA 0x00004000u
#define CK_STATESAVE_2DCURVEPOINTNEWDATA     0x00008000u
#define CK_STATESAVE_2DCURVEPOINTRESERVED1   0x00010000u
#define CK_STATESAVE_2DCURVEPOINTRESERVED2   0x00020000u
#define CK_STATESAVE_2DCURVESAVEPOINTS       0x0003F800u
#define CK_STATESAVE_2DCURVEALL              0x0007FFFFu

/* ============================================================================
 * Kinematic Chain
 * ============================================================================ */

#define CK_STATESAVE_KINEMATICCHAINDATA      0x00000010u
#define CK_STATESAVE_KINEMATICCHAINRESERVED1 0x00000020u
#define CK_STATESAVE_KINEMATICCHAINRESERVED2 0x00000040u
#define CK_STATESAVE_KINEMATICCHAINRESERVED3 0x00000080u
#define CK_STATESAVE_KINEMATICCHAINALL       0x000000FFu

/* ============================================================================
 * Animation
 * ============================================================================ */

#define CK_STATESAVE_ANIMATIONDATA        0x00000010u
#define CK_STATESAVE_ANIMATIONRESERVED1   0x00000020u
#define CK_STATESAVE_ANIMATIONLENGTH      0x00000040u
#define CK_STATESAVE_ANIMATIONBODYPARTS   0x00000080u
#define CK_STATESAVE_ANIMATIONCHARACTER   0x00000100u
#define CK_STATESAVE_ANIMATIONCURRENTSTEP 0x00000200u
#define CK_STATESAVE_ANIMATIONRESERVED5   0x00000400u
#define CK_STATESAVE_ANIMATIONRESERVED6   0x00000800u
#define CK_STATESAVE_ANIMATIONALL         0x0FFFFFFFu

/* ============================================================================
 * Keyed Anim
 * ============================================================================ */

#define CK_STATESAVE_KEYEDANIMANIMLIST  0x00001000u
#define CK_STATESAVE_KEYEDANIMLENGTH    0x00002000u
#define CK_STATESAVE_KEYEDANIMPOSKEYS   0x00004000u
#define CK_STATESAVE_KEYEDANIMROTKEYS   0x00008000u
#define CK_STATESAVE_KEYEDANIMMORPHKEYS 0x00010000u
#define CK_STATESAVE_KEYEDANIMSCLKEYS   0x00020000u
#define CK_STATESAVE_KEYEDANIMFLAGS     0x00040000u
#define CK_STATESAVE_KEYEDANIMENTITY    0x00080000u
#define CK_STATESAVE_KEYEDANIMMERGE     0x00100000u
#define CK_STATESAVE_KEYEDANIMSUBANIMS  0x00200000u
#define CK_STATESAVE_KEYEDANIMRESERVED0 0x00400000u
#define CK_STATESAVE_KEYEDANIMRESERVED1 0x00800000u
#define CK_STATESAVE_KEYEDANIMRESERVED2 0x01000000u
#define CK_STATESAVE_KEYEDANIMRESERVED3 0x02000000u

/* ============================================================================
 * Object Animation
 * ============================================================================ */

#define CK_STATESAVE_OBJANIMNEWDATA       0x00001000u
#define CK_STATESAVE_OBJANIMLENGTH        0x00002000u
#define CK_STATESAVE_OBJANIMPOSKEYS       0x00004000u
#define CK_STATESAVE_OBJANIMROTKEYS       0x00008000u
#define CK_STATESAVE_OBJANIMMORPHKEYS     0x00010000u
#define CK_STATESAVE_OBJANIMSCLKEYS       0x00020000u
#define CK_STATESAVE_OBJANIMFLAGS         0x00040000u
#define CK_STATESAVE_OBJANIMENTITY        0x00080000u
#define CK_STATESAVE_OBJANIMMERGE         0x00100000u
#define CK_STATESAVE_OBJANIMMORPHKEYS2    0x00200000u
#define CK_STATESAVE_OBJANIMNEWSAVE1      0x00400000u
#define CK_STATESAVE_OBJANIMMORPHNORMALS  0x00800000u
#define CK_STATESAVE_OBJANIMMORPHCOMP     0x01000000u
#define CK_STATESAVE_OBJANIMSHARED        0x02000000u
#define CK_STATESAVE_OBJANIMCONTROLLERS   0x04000000u
#define CK_STATESAVE_OBJANIMONLY          0x07FFF000u
#define CK_STATESAVE_OBJANIMALL           0x07FFFFFFu
#define CK_STATESAVE_KEYEDANIMONLY        0x03FFF000u
#define CK_STATESAVE_KEYEDANIMALL         0x03FFFFFFu

/* ============================================================================
 * IK Animation
 * ============================================================================ */

#define CK_STATESAVE_IKANIMATIONDATA      0x00001000u
#define CK_STATESAVE_IKANIMATIONRESERVED2 0x00002000u
#define CK_STATESAVE_IKANIMATIONRESERVED3 0x00004000u
#define CK_STATESAVE_IKANIMATIONRESERVED4 0x00008000u
#define CK_STATESAVE_IKANIMATIONRESERVED5 0x00010000u
#define CK_STATESAVE_IKANIMATIONRESERVED6 0x00020000u
#define CK_STATESAVE_IKANIMATIONRESERVED7 0x00040000u
#define CK_STATESAVE_IKANIMATIONRESERVED8 0x00100000u
#define CK_STATESAVE_IKANIMATIONRESERVED9 0x00200000u
#define CK_STATESAVE_IKANIMATIONALL       0x003FFFFFu

/* ============================================================================
 * BehaviorLink
 * ============================================================================ */

#define CK_STATESAVE_BEHAV_LINK_CURDELAY 0x00000004u
#define CK_STATESAVE_BEHAV_LINK_IOS      0x00000008u
#define CK_STATESAVE_BEHAV_LINK_DELAY    0x00000010u
#define CK_STATESAVE_BEHAV_LINK_NEWDATA  0x00000020u
#define CK_STATESAVE_BEHAV_LINKRESERVED5 0x00000040u
#define CK_STATESAVE_BEHAV_LINKRESERVED6 0x00000080u
#define CK_STATESAVE_BEHAV_LINKONLY      0x000000F0u
#define CK_STATESAVE_BEHAV_LINKALL       0x000000FFu

/* ============================================================================
 * BehaviorIO
 * ============================================================================ */

#define CK_STATESAVE_BEHAV_IOFLAGS     0x00000008u
#define CK_STATESAVE_BEHAV_IORESERVED3 0x00000010u
#define CK_STATESAVE_BEHAV_IORESERVED4 0x00000020u
#define CK_STATESAVE_BEHAV_IORESERVED5 0x00000040u
#define CK_STATESAVE_BEHAV_IORESERVED6 0x00000080u
#define CK_STATESAVE_BEHAVIOONLY       0x000000F0u
#define CK_STATESAVE_BEHAVIOALL        0x000000FFu

/* ============================================================================
 * BehaviorPrototype
 * ============================================================================ */

#define CK_STATESAVE_PROTORESERVED0      0x00000010u
#define CK_STATESAVE_PROTORESERVED1      0x00000020u
#define CK_STATESAVE_PROTOFLAGS          0x00000040u
#define CK_STATESAVE_PROTOSUBPROTOS      0x00000080u
#define CK_STATESAVE_PROTOLINKS          0x00000100u
#define CK_STATESAVE_PROTOBEHAVFLAG      0x00000200u
#define CK_STATESAVE_PROTOGUID           0x00000400u
#define CK_STATESAVE_PROTOINPUTS         0x00000800u
#define CK_STATESAVE_PROTOOUTPUTS        0x00001000u
#define CK_STATESAVE_PROTOINPARAMS       0x00002000u
#define CK_STATESAVE_PROTOOUTPARAMS      0x00004000u
#define CK_STATESAVE_PROTOLOCALPARAMS    0x00008000u
#define CK_STATESAVE_PROTOOPERATIONS     0x00010000u
#define CK_STATESAVE_PROTOPARAMETERLINKS 0x00020000u
#define CK_STATESAVE_PROTOAPPLYTO        0x00040000u
#define CK_STATESAVE_PROTORESERVED14     0x00080000u
#define CK_STATESAVE_PROTOALL            0x000FFFFFu

/* ============================================================================
 * Behavior
 * ============================================================================ */

#define CK_STATESAVE_BEHAVIORINTERFACE      0x00000010u
#define CK_STATESAVE_BEHAVIORNEWDATA        0x00000020u
#define CK_STATESAVE_BEHAVIORFLAGS          0x00000040u
#define CK_STATESAVE_BEHAVIORCOMPATIBLECID  0x00000080u
#define CK_STATESAVE_BEHAVIORSUBBEHAV       0x00000100u
#define CK_STATESAVE_BEHAVIORINPARAMS       0x00000200u
#define CK_STATESAVE_BEHAVIOROUTPARAMS      0x00000400u
#define CK_STATESAVE_BEHAVIORINPUTS         0x00000800u
#define CK_STATESAVE_BEHAVIOROUTPUTS        0x00001000u
#define CK_STATESAVE_BEHAVIORINFO           0x00002000u
#define CK_STATESAVE_BEHAVIOROPERATIONS     0x00004000u
#define CK_STATESAVE_BEHAVIORTYPE           0x00008000u
#define CK_STATESAVE_BEHAVIOROWNER          0x00010000u
#define CK_STATESAVE_BEHAVIORLOCALPARAMS    0x00020000u
#define CK_STATESAVE_BEHAVIORPROTOGUID      0x00040000u
#define CK_STATESAVE_BEHAVIORSUBLINKS       0x00080000u
#define CK_STATESAVE_BEHAVIORACTIVESUBLINKS 0x00100000u
#define CK_STATESAVE_BEHAVIORSINGLEACTIVITY 0x00200000u
#define CK_STATESAVE_BEHAVIORSCRIPTDATA     0x00400000u
#define CK_STATESAVE_BEHAVIORPRIORITY       0x00800000u
#define CK_STATESAVE_BEHAVIORTARGET         0x01000000u
#define CK_STATESAVE_BEHAVIORONLY           0x01FFFFF0u
#define CK_STATESAVE_BEHAVIORALL            0x01FFFFFFu

/* ============================================================================
 * Scene
 * ============================================================================ */

#define CK_STATESAVE_SCENERESERVED0      0x00001000u
#define CK_STATESAVE_SCENERESERVED8      0x00002000u
#define CK_STATESAVE_SCENEFLAGS          0x00004000u
#define CK_STATESAVE_SCENELEVEL          0x00008000u
#define CK_STATESAVE_SCENEOBJECTS        0x00010000u
#define CK_STATESAVE_SCENENEWDATA        0x00020000u
#define CK_STATESAVE_SCENELAUNCHED       0x00040000u
#define CK_STATESAVE_SCENERENDERSETTINGS 0x00080000u
#define CK_STATESAVE_SCENERESERVED1      0x00100000u
#define CK_STATESAVE_SCENERESERVED2      0x00200000u
#define CK_STATESAVE_SCENERESERVED3      0x00400000u
#define CK_STATESAVE_SCENERESERVED4      0x00800000u
#define CK_STATESAVE_SCENERESERVED5      0x01000000u
#define CK_STATESAVE_SCENERESERVED12     0x02000000u
#define CK_STATESAVE_SCENERESERVED13     0x04000000u
#define CK_STATESAVE_SCENERESERVED14     0x08000000u
#define CK_STATESAVE_SCENEALL            0x0FFFFFFFu

/* ============================================================================
 * ParameterIn
 * ============================================================================ */

#define CK_STATESAVE_PARAMETERIN_RESERVED4   0x00000010u
#define CK_STATESAVE_PARAMETERIN_RESERVED0   0x00000020u
#define CK_STATESAVE_PARAMETERIN_RESERVED1   0x00000040u
#define CK_STATESAVE_PARAMETERIN_OWNER       0x00000080u
#define CK_STATESAVE_PARAMETERIN_INSHARED    0x00000100u
#define CK_STATESAVE_PARAMETERIN_OUTSOURCE   0x00000200u
#define CK_STATESAVE_PARAMETERIN_DEFAULTDATA 0x00000400u
#define CK_STATESAVE_PARAMETERIN_DATASHARED  0x00000800u
#define CK_STATESAVE_PARAMETERIN_DATASOURCE  0x00001000u
#define CK_STATESAVE_PARAMETERIN_DISABLED    0x00002000u
#define CK_STATESAVE_PARAMETERIN_ALL         0x0000FFFFu

/* ============================================================================
 * ParameterOut (also ParameterLocal)
 * ============================================================================ */

#define CK_STATESAVE_PARAMETEROUT_RESERVED0    0x00000010u
#define CK_STATESAVE_PARAMETEROUT_DESTINATIONS 0x00000020u
#define CK_STATESAVE_PARAMETEROUT_VAL          0x00000040u
#define CK_STATESAVE_PARAMETEROUT_OWNER        0x00000080u
#define CK_STATESAVE_PARAMETEROUT_MYSELF       0x00000200u
#define CK_STATESAVE_PARAMETEROUT_ISSETTING    0x00000400u
#define CK_STATESAVE_PARAMETEROUT_ALL          0x0000FFFFu

/* ============================================================================
 * Parameter Operation
 * ============================================================================ */

#define CK_STATESAVE_OPERATIONRESERVED0   0x00000010u
#define CK_STATESAVE_OPERATIONRESERVED1   0x00000020u
#define CK_STATESAVE_OPERATIONINPUTS      0x00000040u
#define CK_STATESAVE_OPERATIONOUTPUT      0x00000080u
#define CK_STATESAVE_OPERATIONOP          0x00000100u
#define CK_STATESAVE_OPERATIONDEFAULTDATA 0x00000200u
#define CK_STATESAVE_OPERATIONNEWDATA     0x00000400u
#define CK_STATESAVE_OPERATIONALL         0x000007FFu

/* ============================================================================
 * Synchro
 * ============================================================================ */

#define CK_STATESAVE_SYNCHRODATA      0x00000010u
#define CK_STATESAVE_SYNCHRORESERVED0 0x00000040u
#define CK_STATESAVE_SYNCHRORESERVED1 0x00000080u
#define CK_STATESAVE_SYNCHRORESERVED2 0x00000100u
#define CK_STATESAVE_SYNCHRORESERVED3 0x00000200u
#define CK_STATESAVE_SYNCHRONALL      0x000003FFu

/* ============================================================================
 * Grid
 * ============================================================================ */

#define CK_STATESAVE_GRIDDATA      0x00400000u
#define CK_STATESAVE_GRIDRESERVED0 0x00800000u
#define CK_STATESAVE_GRIDRESERVED1 0x01000000u
#define CK_STATESAVE_GRIDRESERVED2 0x02000000u
#define CK_STATESAVE_GRIDRESERVED3 0x04000000u
#define CK_STATESAVE_GRIDRESERVED4 0x08000000u
#define CK_STATESAVE_GRIDONLY      0x0FC00000u
#define CK_STATESAVE_GRIDALL       0x0FFFFFFFu

/* ============================================================================
 * Layer (for Grids)
 * ============================================================================ */

#define CK_STATESAVE_LAYERDATA      0x00000010u
#define CK_STATESAVE_LAYERRESERVED0 0x00800020u
#define CK_STATESAVE_LAYERRESERVED1 0x00000040u
#define CK_STATESAVE_LAYERRESERVED2 0x00000080u
#define CK_STATESAVE_LAYERRESERVED3 0x00000100u
#define CK_STATESAVE_LAYERRESERVED4 0x00000200u
#define CK_STATESAVE_LAYERONLY      0x000003F0u
#define CK_STATESAVE_LAYERALL       0x000003FFu

/* ============================================================================
 * DataArray
 * ============================================================================ */

#define CK_STATESAVE_DATAARRAYFORMAT  0x00001000u
#define CK_STATESAVE_DATAARRAYDATA    0x00002000u
#define CK_STATESAVE_DATAARRAYMEMBERS 0x00004000u
#define CK_STATESAVE_DATAARRAYALL     0x0000FFFFu

/* ============================================================================
 * SceneObjectDesc
 * ============================================================================ */

#define CK_STATESAVE_SCENEOBJECTDESC     0x00000010u
#define CK_STATESAVE_SCENEOBJECTRES1     0x00000020u
#define CK_STATESAVE_SCENEOBJECTRES2     0x00000040u
#define CK_STATESAVE_SCENEOBJECTRES3     0x00000080u
#define CK_STATESAVE_SCENEOBJECTDESCALL  0x000000FFu

#endif /* NMO_CKSTATESAVE_IDS_H */
