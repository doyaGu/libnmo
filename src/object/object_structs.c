/**
 * @file object_structs.c
 * @brief CK/VX struct type registration for object schemas
 */

#include "object/nmo_object_structs.h"
#include "object/nmo_object_struct_guids.h"
#include "type/nmo_dynamic_types.h"
#include "type/nmo_builtin_type_guids.h"
#include "type/nmo_type_system.h"
#include "core/nmo_error.h"

#define NMO_STRUCT_FIELD_GUID(_name, _guid) \
    { \
        .name = (_name), \
        .type_name = NULL, \
        .type_guid = _guid, \
        .description = NULL, \
        .flags = 0, \
        .default_value = NULL \
    }

#define NMO_STRUCT_FIELD_GUID_FLAGS(_name, _guid, _flags) \
    { \
        .name = (_name), \
        .type_name = NULL, \
        .type_guid = _guid, \
        .description = NULL, \
        .flags = (_flags), \
        .default_value = NULL \
    }

#define NMO_STRUCT_FIELD_NAME(_name, _type_name) \
    { \
        .name = (_name), \
        .type_name = (_type_name), \
        .type_guid = {0, 0}, \
        .description = NULL, \
        .flags = 0, \
        .default_value = NULL \
    }

#define NMO_STRUCT_FIELD_PTR(_name, _flags) \
    { \
        .name = (_name), \
        .type_name = NULL, \
        .type_guid = NMO_GUID_FIELD_POINTER, \
        .description = NULL, \
        .flags = (_flags), \
        .default_value = NULL \
    }

#define NMO_STRUCT_DEF(_name, _guid, _fields) \
    { \
        .name = (_name), \
        .description = NULL, \
        .guid = _guid, \
        .fields = (_fields), \
        .field_count = sizeof(_fields) / sizeof((_fields)[0]), \
        .alignment = 0, \
        .packed = false \
    }

#define NMO_UNION_DEF(_name, _guid, _fields) \
    { \
        .name = (_name), \
        .description = NULL, \
        .guid = _guid, \
        .fields = (_fields), \
        .field_count = sizeof(_fields) / sizeof((_fields)[0]), \
        .alignment = 0, \
        .packed = false \
    }

nmo_status_t nmo_register_object_structs(nmo_type_registry_t *registry) {
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type registry");

    /* ColorF (float RGBA) */
    static const nmo_struct_field_def_t colorf_fields[] = {
        NMO_STRUCT_FIELD_GUID("r", NMO_GUID_FIELD_FLOAT),
        NMO_STRUCT_FIELD_GUID("g", NMO_GUID_FIELD_FLOAT),
        NMO_STRUCT_FIELD_GUID("b", NMO_GUID_FIELD_FLOAT),
        NMO_STRUCT_FIELD_GUID("a", NMO_GUID_FIELD_FLOAT)
    };
    static const nmo_struct_type_def_t colorf_def =
        NMO_STRUCT_DEF("ColorF", NMO_GUID_FIELD_COLORF, colorf_fields);

    /* CKBitmapData */
    static const nmo_struct_field_def_t ckbitmapdata_fields[] = {
        NMO_STRUCT_FIELD_GUID("width", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("height", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_PTR("pixel_data", NMO_FIELD_REPEATED),
        NMO_STRUCT_FIELD_GUID("pixel_data_size", NMO_GUID_FIELD_UINT64),
        NMO_STRUCT_FIELD_PTR("palette_data", NMO_FIELD_REPEATED),
        NMO_STRUCT_FIELD_GUID("palette_size", NMO_GUID_FIELD_UINT64),
        NMO_STRUCT_FIELD_PTR("system_copy_data", NMO_FIELD_REPEATED),
        NMO_STRUCT_FIELD_GUID("system_copy_size", NMO_GUID_FIELD_UINT64),
        NMO_STRUCT_FIELD_PTR("video_backup_data", NMO_FIELD_REPEATED),
        NMO_STRUCT_FIELD_GUID("video_backup_size", NMO_GUID_FIELD_UINT64),
        NMO_STRUCT_FIELD_PTR("pixels_data", NMO_FIELD_REPEATED),
        NMO_STRUCT_FIELD_GUID("pixels_size", NMO_GUID_FIELD_UINT64),
        NMO_STRUCT_FIELD_PTR("raw_chunk_data", NMO_FIELD_REPEATED),
        NMO_STRUCT_FIELD_GUID("raw_chunk_size", NMO_GUID_FIELD_UINT64)
    };
    static const nmo_struct_type_def_t ckbitmapdata_def =
        NMO_STRUCT_DEF("CKBitmapData", NMO_GUID_FIELD_CKBITMAPDATA, ckbitmapdata_fields);

    /* FontInfo */
    static const nmo_struct_field_def_t fontinfo_fields[] = {
        NMO_STRUCT_FIELD_GUID("font_name", NMO_GUID_FIELD_STRING),
        NMO_STRUCT_FIELD_GUID("size", NMO_GUID_FIELD_INT32),
        NMO_STRUCT_FIELD_GUID("weight", NMO_GUID_FIELD_INT32),
        NMO_STRUCT_FIELD_GUID("italic", NMO_GUID_FIELD_INT32),
        NMO_STRUCT_FIELD_GUID("underline", NMO_GUID_FIELD_INT32)
    };
    static const nmo_struct_type_def_t fontinfo_def =
        NMO_STRUCT_DEF("FontInfo", NMO_GUID_FIELD_FONTINFO, fontinfo_fields);

    /* CKLightData */
    static const nmo_struct_field_def_t cklightdata_fields[] = {
        NMO_STRUCT_FIELD_GUID("type", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("diffuse", NMO_GUID_FIELD_COLORF),
        NMO_STRUCT_FIELD_GUID("specular", NMO_GUID_FIELD_COLORF),
        NMO_STRUCT_FIELD_GUID("ambient", NMO_GUID_FIELD_COLORF),
        NMO_STRUCT_FIELD_GUID("position", NMO_GUID_FIELD_VECTOR3),
        NMO_STRUCT_FIELD_GUID("direction", NMO_GUID_FIELD_VECTOR3),
        NMO_STRUCT_FIELD_GUID("range", NMO_GUID_FIELD_FLOAT),
        NMO_STRUCT_FIELD_GUID("falloff", NMO_GUID_FIELD_FLOAT),
        NMO_STRUCT_FIELD_GUID("attenuation0", NMO_GUID_FIELD_FLOAT),
        NMO_STRUCT_FIELD_GUID("attenuation1", NMO_GUID_FIELD_FLOAT),
        NMO_STRUCT_FIELD_GUID("attenuation2", NMO_GUID_FIELD_FLOAT),
        NMO_STRUCT_FIELD_GUID("inner_spot_cone", NMO_GUID_FIELD_FLOAT),
        NMO_STRUCT_FIELD_GUID("outer_spot_cone", NMO_GUID_FIELD_FLOAT)
    };
    static const nmo_struct_type_def_t cklightdata_def =
        NMO_STRUCT_DEF("CKLightData", NMO_GUID_FIELD_CKLIGHTDATA, cklightdata_fields);

    /* CKSceneObjectDesc */
    static const nmo_struct_field_def_t cksceneobjectdesc_fields[] = {
        NMO_STRUCT_FIELD_GUID_FLAGS("object_id", NMO_GUID_FIELD_OBJECT_ID, NMO_FIELD_REFERENCE),
        NMO_STRUCT_FIELD_GUID("initial_value", NMO_GUID_FIELD_CHUNK),
        NMO_STRUCT_FIELD_GUID("flags", NMO_GUID_FIELD_UINT32)
    };
    static const nmo_struct_type_def_t cksceneobjectdesc_def =
        NMO_STRUCT_DEF("CKSceneObjectDesc", NMO_GUID_FIELD_CKSCENEOBJECTDESC, cksceneobjectdesc_fields);

    /* CKPlacePortalEntry */
    static const nmo_struct_field_def_t ckplaceportalentry_fields[] = {
        NMO_STRUCT_FIELD_GUID_FLAGS("place_id", NMO_GUID_FIELD_OBJECT_ID, NMO_FIELD_REFERENCE),
        NMO_STRUCT_FIELD_GUID_FLAGS("portal_id", NMO_GUID_FIELD_OBJECT_ID, NMO_FIELD_REFERENCE)
    };
    static const nmo_struct_type_def_t ckplaceportalentry_def =
        NMO_STRUCT_DEF("CKPlacePortalEntry", NMO_GUID_FIELD_CKPLACEPORTALENTRY, ckplaceportalentry_fields);

    /* CKPatchMeshPatch */
    static const nmo_struct_field_def_t ckpatchmeshpatch_fields[] = {
        NMO_STRUCT_FIELD_GUID("type", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("smoothing_group", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_NAME("data", "uint8[40]")
    };
    static const nmo_struct_type_def_t ckpatchmeshpatch_def =
        NMO_STRUCT_DEF("CKPatchMeshPatch", NMO_GUID_FIELD_CKPATCHMESHPATCH, ckpatchmeshpatch_fields);

    /* CKPatchMeshChannel */
    static const nmo_struct_field_def_t ckpatchmeshchannel_fields[] = {
        NMO_STRUCT_FIELD_GUID_FLAGS("material_id", NMO_GUID_FIELD_OBJECT_ID, NMO_FIELD_REFERENCE),
        NMO_STRUCT_FIELD_GUID("flags", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("type", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("subtype", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("patch_count", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_PTR("patches_raw", NMO_FIELD_REPEATED),
        NMO_STRUCT_FIELD_GUID("uv_count", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_PTR("uvs", NMO_FIELD_REPEATED)
    };
    static const nmo_struct_type_def_t ckpatchmeshchannel_def =
        NMO_STRUCT_DEF("CKPatchMeshChannel", NMO_GUID_FIELD_CKPATCHMESHCHANNEL, ckpatchmeshchannel_fields);

    /* CKDataArrayColumnFormat */
    static const nmo_struct_field_def_t ckdataarraycolumnformat_fields[] = {
        NMO_STRUCT_FIELD_GUID("name", NMO_GUID_FIELD_STRING),
        NMO_STRUCT_FIELD_GUID("type", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("parameter_type_guid", NMO_GUID_FIELD_GUID)
    };
    static const nmo_struct_type_def_t ckdataarraycolumnformat_def =
        NMO_STRUCT_DEF("CKDataArrayColumnFormat", NMO_GUID_FIELD_CKDATAARRAYCOLUMNFORMAT, ckdataarraycolumnformat_fields);

    /* CKDataArrayCell (union) */
    static const nmo_struct_field_def_t ckdataarraycell_fields[] = {
        NMO_STRUCT_FIELD_GUID("int_value", NMO_GUID_FIELD_INT32),
        NMO_STRUCT_FIELD_GUID("float_value", NMO_GUID_FIELD_FLOAT),
        NMO_STRUCT_FIELD_GUID("string_value", NMO_GUID_FIELD_STRING),
        NMO_STRUCT_FIELD_GUID_FLAGS("object_id", NMO_GUID_FIELD_OBJECT_ID, NMO_FIELD_REFERENCE),
        NMO_STRUCT_FIELD_GUID_FLAGS("parameter_id", NMO_GUID_FIELD_OBJECT_ID, NMO_FIELD_REFERENCE),
        NMO_STRUCT_FIELD_GUID("parameter_chunk", NMO_GUID_FIELD_CHUNK)
    };
    static const nmo_union_type_def_t ckdataarraycell_def =
        NMO_UNION_DEF("CKDataArrayCell", NMO_GUID_FIELD_CKDATAARRAYCELL, ckdataarraycell_fields);

    /* CKDataArrayRow */
    static const nmo_struct_field_def_t ckdataarrayrow_fields[] = {
        NMO_STRUCT_FIELD_GUID("column_count", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_PTR("cells", NMO_FIELD_REPEATED)
    };
    static const nmo_struct_type_def_t ckdataarrayrow_def =
        NMO_STRUCT_DEF("CKDataArrayRow", NMO_GUID_FIELD_CKDATAARRAYROW, ckdataarrayrow_fields);

    /* CKCurvePointSubchunk */
    static const nmo_struct_field_def_t ckcurvepointsubchunk_fields[] = {
        NMO_STRUCT_FIELD_GUID_FLAGS("point_id", NMO_GUID_FIELD_OBJECT_ID, NMO_FIELD_REFERENCE),
        NMO_STRUCT_FIELD_GUID("chunk", NMO_GUID_FIELD_CHUNK)
    };
    static const nmo_struct_type_def_t ckcurvepointsubchunk_def =
        NMO_STRUCT_DEF("CKCurvePointSubchunk", NMO_GUID_FIELD_CKCURVEPOINTSUBCHUNK, ckcurvepointsubchunk_fields);

    /* CKCharacterSubpart */
    static const nmo_struct_field_def_t ckcharactersubpart_fields[] = {
        NMO_STRUCT_FIELD_GUID_FLAGS("object_id", NMO_GUID_FIELD_OBJECT_ID, NMO_FIELD_REFERENCE),
        NMO_STRUCT_FIELD_GUID("chunk", NMO_GUID_FIELD_CHUNK)
    };
    static const nmo_struct_type_def_t ckcharactersubpart_def =
        NMO_STRUCT_DEF("CKCharacterSubpart", NMO_GUID_FIELD_CKCHARACTERSUBPART, ckcharactersubpart_fields);

    /* CKIKJoint */
    static const nmo_struct_field_def_t ckikjoint_fields[] = {
        NMO_STRUCT_FIELD_GUID("flags", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("min", NMO_GUID_FIELD_VECTOR3),
        NMO_STRUCT_FIELD_GUID("max", NMO_GUID_FIELD_VECTOR3),
        NMO_STRUCT_FIELD_GUID("damping", NMO_GUID_FIELD_VECTOR3)
    };
    static const nmo_struct_type_def_t ckikjoint_def =
        NMO_STRUCT_DEF("CKIKJoint", NMO_GUID_FIELD_CKIKJOINT, ckikjoint_fields);

    /* VxVertex */
    static const nmo_struct_field_def_t vxvertex_fields[] = {
        NMO_STRUCT_FIELD_GUID("position", NMO_GUID_FIELD_VECTOR3),
        NMO_STRUCT_FIELD_GUID("normal", NMO_GUID_FIELD_VECTOR3),
        NMO_STRUCT_FIELD_GUID("uv", NMO_GUID_FIELD_VECTOR2)
    };
    static const nmo_struct_type_def_t vxvertex_def =
        NMO_STRUCT_DEF("VxVertex", NMO_GUID_FIELD_VXVERTEX, vxvertex_fields);

    /* CKFace */
    static const nmo_struct_field_def_t ckface_fields[] = {
        NMO_STRUCT_FIELD_GUID("normal", NMO_GUID_FIELD_VECTOR3),
        NMO_STRUCT_FIELD_GUID("material_group_idx", NMO_GUID_FIELD_UINT16),
        NMO_STRUCT_FIELD_GUID("channel_mask", NMO_GUID_FIELD_UINT16)
    };
    static const nmo_struct_type_def_t ckface_def =
        NMO_STRUCT_DEF("CKFace", NMO_GUID_FIELD_CKFACE, ckface_fields);

    /* CKMaterialChannel */
    static const nmo_struct_field_def_t ckmaterialchannel_fields[] = {
        NMO_STRUCT_FIELD_GUID_FLAGS("material_id", NMO_GUID_FIELD_OBJECT_ID, NMO_FIELD_REFERENCE),
        NMO_STRUCT_FIELD_GUID("flags", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("source_blend", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("dest_blend", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("uv_count", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_PTR("uv_coords", NMO_FIELD_REPEATED)
    };
    static const nmo_struct_type_def_t ckmaterialchannel_def =
        NMO_STRUCT_DEF("CKMaterialChannel", NMO_GUID_FIELD_CKMATERIALCHANNEL, ckmaterialchannel_fields);

    /* CKMaterialGroup */
    static const nmo_struct_field_def_t ckmaterialgroup_fields[] = {
        NMO_STRUCT_FIELD_GUID_FLAGS("material_id", NMO_GUID_FIELD_OBJECT_ID, NMO_FIELD_REFERENCE)
    };
    static const nmo_struct_type_def_t ckmaterialgroup_def =
        NMO_STRUCT_DEF("CKMaterialGroup", NMO_GUID_FIELD_CKMATERIALGROUP, ckmaterialgroup_fields);

    /* TextureFormat */
    static const nmo_struct_field_def_t textureformat_fields[] = {
        NMO_STRUCT_FIELD_GUID("width", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("height", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("bits_per_pixel", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("bytes_per_line", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("image_size", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("red_mask", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("green_mask", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("blue_mask", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("alpha_mask", NMO_GUID_FIELD_UINT32)
    };
    static const nmo_struct_type_def_t textureformat_def =
        NMO_STRUCT_DEF("TextureFormat", NMO_GUID_FIELD_TEXTUREFORMAT, textureformat_fields);

    /* MipmapLevel */
    static const nmo_struct_field_def_t mipmaplevel_fields[] = {
        NMO_STRUCT_FIELD_GUID("width", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("height", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("size", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_PTR("data", NMO_FIELD_REPEATED)
    };
    static const nmo_struct_type_def_t mipmaplevel_def =
        NMO_STRUCT_DEF("MipmapLevel", NMO_GUID_FIELD_MIPMAPLEVEL, mipmaplevel_fields);

    /* CKTextureReaderSlot */
    static const nmo_struct_field_def_t cktexturereaderslot_fields[] = {
        NMO_STRUCT_FIELD_GUID("format_type", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("extension", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("reader_guid", NMO_GUID_FIELD_GUID),
        NMO_STRUCT_FIELD_GUID("data_size", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_PTR("data", NMO_FIELD_REPEATED),
        NMO_STRUCT_FIELD_GUID("alpha_count", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("alpha_value", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("alpha_plane_size", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_PTR("alpha_plane", NMO_FIELD_REPEATED)
    };
    static const nmo_struct_type_def_t cktexturereaderslot_def =
        NMO_STRUCT_DEF("CKTextureReaderSlot", NMO_GUID_FIELD_CKTEXTUREREADERSLOT, cktexturereaderslot_fields);

    /* CKTextureRawSlot */
    static const nmo_struct_field_def_t cktexturerawslot_fields[] = {
        NMO_STRUCT_FIELD_GUID("bits_per_pixel", NMO_GUID_FIELD_INT32),
        NMO_STRUCT_FIELD_GUID("width", NMO_GUID_FIELD_INT32),
        NMO_STRUCT_FIELD_GUID("height", NMO_GUID_FIELD_INT32),
        NMO_STRUCT_FIELD_GUID("alpha_mask", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("red_mask", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("green_mask", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("blue_mask", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("compression", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("blue_size", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_PTR("blue_data", NMO_FIELD_REPEATED),
        NMO_STRUCT_FIELD_GUID("green_size", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_PTR("green_data", NMO_FIELD_REPEATED),
        NMO_STRUCT_FIELD_GUID("red_size", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_PTR("red_data", NMO_FIELD_REPEATED),
        NMO_STRUCT_FIELD_GUID("alpha_size", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_PTR("alpha_data", NMO_FIELD_REPEATED)
    };
    static const nmo_struct_type_def_t cktexturerawslot_def =
        NMO_STRUCT_DEF("CKTextureRawSlot", NMO_GUID_FIELD_CKTEXTURERAWSLOT, cktexturerawslot_fields);

    /* CKTextureBitmap2Slot */
    static const nmo_struct_field_def_t cktexturebitmap2slot_fields[] = {
        NMO_STRUCT_FIELD_GUID("header_size", NMO_GUID_FIELD_INT32),
        NMO_STRUCT_FIELD_GUID("buffer_size", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_PTR("buffer", NMO_FIELD_REPEATED)
    };
    static const nmo_struct_type_def_t cktexturebitmap2slot_def =
        NMO_STRUCT_DEF("CKTextureBitmap2Slot", NMO_GUID_FIELD_CKTEXTUREBITMAP2SLOT, cktexturebitmap2slot_fields);

    /* MaterialColors */
    static const nmo_struct_field_def_t materialcolors_fields[] = {
        NMO_STRUCT_FIELD_GUID("ambient", NMO_GUID_FIELD_COLORF),
        NMO_STRUCT_FIELD_GUID("diffuse", NMO_GUID_FIELD_COLORF),
        NMO_STRUCT_FIELD_GUID("specular", NMO_GUID_FIELD_COLORF),
        NMO_STRUCT_FIELD_GUID("emissive", NMO_GUID_FIELD_COLORF)
    };
    static const nmo_struct_type_def_t materialcolors_def =
        NMO_STRUCT_DEF("MaterialColors", NMO_GUID_FIELD_MATERIALCOLORS, materialcolors_fields);

    /* CKAttributeCategory */
    static const nmo_struct_field_def_t ckattributecategory_fields[] = {
        NMO_STRUCT_FIELD_GUID("name", NMO_GUID_FIELD_STRING),
        NMO_STRUCT_FIELD_GUID("flags", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("present", NMO_GUID_FIELD_BOOL)
    };
    static const nmo_struct_type_def_t ckattributecategory_def =
        NMO_STRUCT_DEF("CKAttributeCategory", NMO_GUID_FIELD_CKATTRIBUTECATEGORY, ckattributecategory_fields);

    /* CKAttributeDescriptor */
    static const nmo_struct_field_def_t ckattributedescriptor_fields[] = {
        NMO_STRUCT_FIELD_GUID("name", NMO_GUID_FIELD_STRING),
        NMO_STRUCT_FIELD_GUID("parameter_type_guid", NMO_GUID_FIELD_GUID),
        NMO_STRUCT_FIELD_GUID("category_index", NMO_GUID_FIELD_INT32),
        NMO_STRUCT_FIELD_GUID("compatible_class_id", NMO_GUID_FIELD_INT32),
        NMO_STRUCT_FIELD_GUID("flags", NMO_GUID_FIELD_UINT32),
        NMO_STRUCT_FIELD_GUID("present", NMO_GUID_FIELD_BOOL)
    };
    static const nmo_struct_type_def_t ckattributedescriptor_def =
        NMO_STRUCT_DEF("CKAttributeDescriptor", NMO_GUID_FIELD_CKATTRIBUTEDESCRIPTOR, ckattributedescriptor_fields);

    /* CKKeyedAnimationSubanim */
    static const nmo_struct_field_def_t ckkeyedanimationsubanim_fields[] = {
        NMO_STRUCT_FIELD_GUID_FLAGS("object_id", NMO_GUID_FIELD_OBJECT_ID, NMO_FIELD_REFERENCE),
        NMO_STRUCT_FIELD_GUID("chunk", NMO_GUID_FIELD_CHUNK)
    };
    static const nmo_struct_type_def_t ckkeyedanimationsubanim_def =
        NMO_STRUCT_DEF("CKKeyedAnimationSubanim", NMO_GUID_FIELD_CKKEYEDANIMATIONSUBANIM,
                       ckkeyedanimationsubanim_fields);

    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &colorf_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &ckbitmapdata_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &fontinfo_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &cklightdata_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &cksceneobjectdesc_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &ckplaceportalentry_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &ckpatchmeshpatch_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &ckpatchmeshchannel_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &ckdataarraycolumnformat_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_union(registry, &ckdataarraycell_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &ckdataarrayrow_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &ckcurvepointsubchunk_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &ckcharactersubpart_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &ckikjoint_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &vxvertex_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &ckface_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &ckmaterialchannel_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &ckmaterialgroup_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &textureformat_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &mipmaplevel_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &cktexturereaderslot_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &cktexturerawslot_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &cktexturebitmap2slot_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &materialcolors_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &ckattributecategory_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &ckattributedescriptor_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &ckkeyedanimationsubanim_def, NULL));

    NMO_RETURN_OK();
}
