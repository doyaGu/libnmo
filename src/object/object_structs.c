/**
 * @file object_structs.c
 * @brief CK/VX struct type registration for object schemas
 */

#include "object/nmo_object_structs.h"
#include "object/nmo_object_struct_guids.h"
#include "object/nmo_param_guids.h"
#include "type/nmo_dynamic_types.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_type_system.h"
#include "core/nmo_error.h"
#include "object/nmo_object_enum_guids.h"

#define NMO_STRUCT_FIELD_GUID(_name, _guid) \
    { \
        .name = (_name), \
        .type_name = NULL, \
        .type_guid = _guid##_INIT, \
        .description = NULL, \
        .flags = 0, \
        .default_value = NULL \
    }

#define NMO_STRUCT_FIELD_GUID_FLAGS(_name, _guid, _flags) \
    { \
        .name = (_name), \
        .type_name = NULL, \
        .type_guid = _guid##_INIT, \
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
        .type_guid = CKPGUID_POINTER_INIT, \
        .description = NULL, \
        .flags = (_flags), \
        .default_value = NULL \
    }

#define NMO_STRUCT_DEF(_name, _guid, _fields) \
    { \
        .name = (_name), \
        .description = NULL, \
        .guid = _guid##_INIT, \
        .fields = (_fields), \
        .field_count = sizeof(_fields) / sizeof((_fields)[0]), \
        .alignment = 0, \
        .packed = false \
    }

#define NMO_UNION_DEF(_name, _guid, _fields) \
    { \
        .name = (_name), \
        .description = NULL, \
        .guid = _guid##_INIT, \
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
        NMO_STRUCT_FIELD_GUID("r", CKPGUID_FLOAT),
        NMO_STRUCT_FIELD_GUID("g", CKPGUID_FLOAT),
        NMO_STRUCT_FIELD_GUID("b", CKPGUID_FLOAT),
        NMO_STRUCT_FIELD_GUID("a", CKPGUID_FLOAT)
    };
    static const nmo_struct_type_def_t colorf_def =
        NMO_STRUCT_DEF("ColorF", CKPGUID_COLORF, colorf_fields);

    /* CKBitmapData */
    static const nmo_struct_field_def_t ckbitmapdata_fields[] = {
        NMO_STRUCT_FIELD_GUID("width", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("height", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_PTR("pixel_data", NMO_FIELD_REPEATED),
        NMO_STRUCT_FIELD_GUID("pixel_data_size", CKPGUID_UINT64),
        NMO_STRUCT_FIELD_PTR("palette_data", NMO_FIELD_REPEATED),
        NMO_STRUCT_FIELD_GUID("palette_size", CKPGUID_UINT64),
        NMO_STRUCT_FIELD_PTR("system_copy_data", NMO_FIELD_REPEATED),
        NMO_STRUCT_FIELD_GUID("system_copy_size", CKPGUID_UINT64),
        NMO_STRUCT_FIELD_PTR("video_backup_data", NMO_FIELD_REPEATED),
        NMO_STRUCT_FIELD_GUID("video_backup_size", CKPGUID_UINT64),
        NMO_STRUCT_FIELD_PTR("pixels_data", NMO_FIELD_REPEATED),
        NMO_STRUCT_FIELD_GUID("pixels_size", CKPGUID_UINT64),
        NMO_STRUCT_FIELD_PTR("raw_chunk_data", NMO_FIELD_REPEATED),
        NMO_STRUCT_FIELD_GUID("raw_chunk_size", CKPGUID_UINT64)
    };
    static const nmo_struct_type_def_t ckbitmapdata_def =
        NMO_STRUCT_DEF("CKBitmapData", NMO_GUID_STRUCT_CKBITMAPDATA, ckbitmapdata_fields);

    /* FontInfo */
    static const nmo_struct_field_def_t fontinfo_fields[] = {
        NMO_STRUCT_FIELD_GUID("font_name", CKPGUID_STRING),
        NMO_STRUCT_FIELD_GUID("size", CKPGUID_INT),
        NMO_STRUCT_FIELD_GUID("weight", CKPGUID_INT),
        NMO_STRUCT_FIELD_GUID("italic", CKPGUID_INT),
        NMO_STRUCT_FIELD_GUID("underline", CKPGUID_INT)
    };
    static const nmo_struct_type_def_t fontinfo_def =
        NMO_STRUCT_DEF("FontInfo", NMO_GUID_STRUCT_FONTINFO, fontinfo_fields);

    /* CKLightData */
    static const nmo_struct_field_def_t cklightdata_fields[] = {
        NMO_STRUCT_FIELD_GUID("type", CKPGUID_LIGHTTYPE),
        NMO_STRUCT_FIELD_GUID("diffuse", CKPGUID_COLORF),
        NMO_STRUCT_FIELD_GUID("specular", CKPGUID_COLORF),
        NMO_STRUCT_FIELD_GUID("ambient", CKPGUID_COLORF),
        NMO_STRUCT_FIELD_GUID("position", CKPGUID_VECTOR),
        NMO_STRUCT_FIELD_GUID("direction", CKPGUID_VECTOR),
        NMO_STRUCT_FIELD_GUID("range", CKPGUID_FLOAT),
        NMO_STRUCT_FIELD_GUID("falloff", CKPGUID_FLOAT),
        NMO_STRUCT_FIELD_GUID("attenuation0", CKPGUID_FLOAT),
        NMO_STRUCT_FIELD_GUID("attenuation1", CKPGUID_FLOAT),
        NMO_STRUCT_FIELD_GUID("attenuation2", CKPGUID_FLOAT),
        NMO_STRUCT_FIELD_GUID("inner_spot_cone", CKPGUID_FLOAT),
        NMO_STRUCT_FIELD_GUID("outer_spot_cone", CKPGUID_FLOAT)
    };
    static const nmo_struct_type_def_t cklightdata_def =
        NMO_STRUCT_DEF("CKLightData", NMO_GUID_STRUCT_CKLIGHTDATA, cklightdata_fields);

    /* CK3dEntitySkinVertex */
    static const nmo_struct_field_def_t ck3dentityskinvertex_fields[] = {
        NMO_STRUCT_FIELD_GUID("bone_count", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("initial_pos", CKPGUID_VECTOR),
        NMO_STRUCT_FIELD_PTR("bone_indices", NMO_FIELD_REPEATED),
        NMO_STRUCT_FIELD_PTR("bone_weights", NMO_FIELD_REPEATED)
    };
    static const nmo_struct_type_def_t ck3dentityskinvertex_def =
        NMO_STRUCT_DEF("CK3dEntitySkinVertex", CKPGUID_CK3DENTITYSKINVERTEX,
                       ck3dentityskinvertex_fields);

    /* CK3dEntitySkinBone */
    static const nmo_struct_field_def_t ck3dentityskinbone_fields[] = {
        NMO_STRUCT_FIELD_GUID_FLAGS("bone_id", CKPGUID_ID, NMO_FIELD_REFERENCE),
        NMO_STRUCT_FIELD_GUID("bone_flags", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("inverse_bind_matrix", CKPGUID_MATRIX)
    };
    static const nmo_struct_type_def_t ck3dentityskinbone_def =
        NMO_STRUCT_DEF("CK3dEntitySkinBone", CKPGUID_CK3DENTITYSKINBONE,
                       ck3dentityskinbone_fields);

    /* CK3dEntitySkin */
    static const nmo_struct_field_def_t ck3dentityskin_fields[] = {
        NMO_STRUCT_FIELD_GUID("object_init_matrix", CKPGUID_MATRIX),
        NMO_STRUCT_FIELD_GUID("bone_count", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_PTR("bones", NMO_FIELD_REPEATED),
        NMO_STRUCT_FIELD_GUID("vertex_count", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_PTR("vertices", NMO_FIELD_REPEATED),
        NMO_STRUCT_FIELD_GUID("normal_count", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_PTR("normals", NMO_FIELD_REPEATED)
    };
    static const nmo_struct_type_def_t ck3dentityskin_def =
        NMO_STRUCT_DEF("CK3dEntitySkin", CKPGUID_CK3DENTITYSKIN, ck3dentityskin_fields);

    /* CKSceneObjectDesc */
    static const nmo_struct_field_def_t cksceneobjectdesc_fields[] = {
        NMO_STRUCT_FIELD_GUID_FLAGS("object_id", CKPGUID_ID, NMO_FIELD_REFERENCE),
        NMO_STRUCT_FIELD_GUID("initial_value", CKPGUID_STATECHUNK),
        NMO_STRUCT_FIELD_GUID("flags", CKPGUID_UINT32)
    };
    static const nmo_struct_type_def_t cksceneobjectdesc_def =
        NMO_STRUCT_DEF("CKSceneObjectDesc", NMO_GUID_STRUCT_CKSCENEOBJECTDESC, cksceneobjectdesc_fields);

    /* CKPlacePortalEntry */
    static const nmo_struct_field_def_t ckplaceportalentry_fields[] = {
        NMO_STRUCT_FIELD_GUID_FLAGS("place_id", CKPGUID_ID, NMO_FIELD_REFERENCE),
        NMO_STRUCT_FIELD_GUID_FLAGS("portal_id", CKPGUID_ID, NMO_FIELD_REFERENCE)
    };
    static const nmo_struct_type_def_t ckplaceportalentry_def =
        NMO_STRUCT_DEF("CKPlacePortalEntry", NMO_GUID_STRUCT_CKPLACEPORTALENTRY, ckplaceportalentry_fields);

    /* CKPatchMeshPatch */
    static const nmo_struct_field_def_t ckpatchmeshpatch_fields[] = {
        NMO_STRUCT_FIELD_GUID("type", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("smoothing_group", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_NAME("data", "uint8[40]")
    };
    static const nmo_struct_type_def_t ckpatchmeshpatch_def =
        NMO_STRUCT_DEF("CKPatchMeshPatch", NMO_GUID_STRUCT_CKPATCHMESHPATCH, ckpatchmeshpatch_fields);

    /* CKPatchMeshChannel */
    static const nmo_struct_field_def_t ckpatchmeshchannel_fields[] = {
        NMO_STRUCT_FIELD_GUID_FLAGS("material_id", CKPGUID_ID, NMO_FIELD_REFERENCE),
        NMO_STRUCT_FIELD_GUID("flags", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("type", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("subtype", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("patch_count", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_PTR("patches_raw", NMO_FIELD_REPEATED),
        NMO_STRUCT_FIELD_GUID("uv_count", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_PTR("uvs", NMO_FIELD_REPEATED)
    };
    static const nmo_struct_type_def_t ckpatchmeshchannel_def =
        NMO_STRUCT_DEF("CKPatchMeshChannel", NMO_GUID_STRUCT_CKPATCHMESHCHANNEL, ckpatchmeshchannel_fields);

    /* CKDataArrayColumnFormat */
    static const nmo_struct_field_def_t ckdataarraycolumnformat_fields[] = {
        NMO_STRUCT_FIELD_GUID("name", CKPGUID_STRING),
        NMO_STRUCT_FIELD_GUID("type", NMO_GUID_ENUM_CK_ARRAYTYPE),
        NMO_STRUCT_FIELD_GUID("parameter_type_guid", CKPGUID_GUID)
    };
    static const nmo_struct_type_def_t ckdataarraycolumnformat_def =
        NMO_STRUCT_DEF("CKDataArrayColumnFormat", NMO_GUID_STRUCT_CKDATAARRAYCOLUMNFORMAT, ckdataarraycolumnformat_fields);

    /* CKDataArrayCell (union) */
    static const nmo_struct_field_def_t ckdataarraycell_fields[] = {
        NMO_STRUCT_FIELD_GUID("int_value", CKPGUID_INT),
        NMO_STRUCT_FIELD_GUID("float_value", CKPGUID_FLOAT),
        NMO_STRUCT_FIELD_GUID("string_value", CKPGUID_STRING),
        NMO_STRUCT_FIELD_GUID_FLAGS("object_id", CKPGUID_ID, NMO_FIELD_REFERENCE),
        NMO_STRUCT_FIELD_GUID_FLAGS("parameter_id", CKPGUID_ID, NMO_FIELD_REFERENCE),
        NMO_STRUCT_FIELD_GUID("parameter_chunk", CKPGUID_STATECHUNK)
    };
    static const nmo_union_type_def_t ckdataarraycell_def =
        NMO_UNION_DEF("CKDataArrayCell", NMO_GUID_STRUCT_CKDATAARRAYCELL, ckdataarraycell_fields);

    /* CKDataArrayRow */
    static const nmo_struct_field_def_t ckdataarrayrow_fields[] = {
        NMO_STRUCT_FIELD_GUID("column_count", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_PTR("cells", NMO_FIELD_REPEATED)
    };
    static const nmo_struct_type_def_t ckdataarrayrow_def =
        NMO_STRUCT_DEF("CKDataArrayRow", NMO_GUID_STRUCT_CKDATAARRAYROW, ckdataarrayrow_fields);

    /* CKCurvePointSubchunk */
    static const nmo_struct_field_def_t ckcurvepointsubchunk_fields[] = {
        NMO_STRUCT_FIELD_GUID_FLAGS("point_id", CKPGUID_ID, NMO_FIELD_REFERENCE),
        NMO_STRUCT_FIELD_GUID("chunk", CKPGUID_STATECHUNK)
    };
    static const nmo_struct_type_def_t ckcurvepointsubchunk_def =
        NMO_STRUCT_DEF("CKCurvePointSubchunk", NMO_GUID_STRUCT_CKCURVEPOINTSUBCHUNK, ckcurvepointsubchunk_fields);

    /* CKCharacterSubpart */
    static const nmo_struct_field_def_t ckcharactersubpart_fields[] = {
        NMO_STRUCT_FIELD_GUID_FLAGS("object_id", CKPGUID_ID, NMO_FIELD_REFERENCE),
        NMO_STRUCT_FIELD_GUID("chunk", CKPGUID_STATECHUNK)
    };
    static const nmo_struct_type_def_t ckcharactersubpart_def =
        NMO_STRUCT_DEF("CKCharacterSubpart", NMO_GUID_STRUCT_CKCHARACTERSUBPART, ckcharactersubpart_fields);

    /* CKIKJoint */
    static const nmo_struct_field_def_t ckikjoint_fields[] = {
        NMO_STRUCT_FIELD_GUID("flags", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("min", CKPGUID_VECTOR),
        NMO_STRUCT_FIELD_GUID("max", CKPGUID_VECTOR),
        NMO_STRUCT_FIELD_GUID("damping", CKPGUID_VECTOR)
    };
    static const nmo_struct_type_def_t ckikjoint_def =
        NMO_STRUCT_DEF("CKIKJoint", NMO_GUID_STRUCT_CKIKJOINT, ckikjoint_fields);

    /* VxVertex */
    static const nmo_struct_field_def_t vxvertex_fields[] = {
        NMO_STRUCT_FIELD_GUID("position", CKPGUID_VECTOR),
        NMO_STRUCT_FIELD_GUID("normal", CKPGUID_VECTOR),
        NMO_STRUCT_FIELD_GUID("uv", CKPGUID_2DVECTOR)
    };
    static const nmo_struct_type_def_t vxvertex_def =
        NMO_STRUCT_DEF("VxVertex", NMO_GUID_STRUCT_VXVERTEX, vxvertex_fields);

    /* CKFace */
    static const nmo_struct_field_def_t ckface_fields[] = {
        NMO_STRUCT_FIELD_GUID("normal", CKPGUID_VECTOR),
        NMO_STRUCT_FIELD_GUID("material_group_idx", CKPGUID_UINT16),
        NMO_STRUCT_FIELD_GUID("channel_mask", CKPGUID_UINT16)
    };
    static const nmo_struct_type_def_t ckface_def =
        NMO_STRUCT_DEF("CKFace", NMO_GUID_STRUCT_CKFACE, ckface_fields);

    /* CKMaterialChannel */
    static const nmo_struct_field_def_t ckmaterialchannel_fields[] = {
        NMO_STRUCT_FIELD_GUID_FLAGS("material_id", CKPGUID_ID, NMO_FIELD_REFERENCE),
        NMO_STRUCT_FIELD_GUID("flags", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("source_blend", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("dest_blend", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("uv_count", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_PTR("uv_coords", NMO_FIELD_REPEATED)
    };
    static const nmo_struct_type_def_t ckmaterialchannel_def =
        NMO_STRUCT_DEF("CKMaterialChannel", NMO_GUID_STRUCT_CKMATERIALCHANNEL, ckmaterialchannel_fields);

    /* CKMaterialGroup */
    static const nmo_struct_field_def_t ckmaterialgroup_fields[] = {
        NMO_STRUCT_FIELD_GUID_FLAGS("material_id", CKPGUID_ID, NMO_FIELD_REFERENCE)
    };
    static const nmo_struct_type_def_t ckmaterialgroup_def =
        NMO_STRUCT_DEF("CKMaterialGroup", NMO_GUID_STRUCT_CKMATERIALGROUP, ckmaterialgroup_fields);

    /* TextureFormat */
    static const nmo_struct_field_def_t textureformat_fields[] = {
        NMO_STRUCT_FIELD_GUID("width", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("height", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("bits_per_pixel", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("bytes_per_line", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("image_size", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("red_mask", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("green_mask", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("blue_mask", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("alpha_mask", CKPGUID_UINT32)
    };
    static const nmo_struct_type_def_t textureformat_def =
        NMO_STRUCT_DEF("TextureFormat", NMO_GUID_STRUCT_TEXTUREFORMAT, textureformat_fields);

    /* MipmapLevel */
    static const nmo_struct_field_def_t mipmaplevel_fields[] = {
        NMO_STRUCT_FIELD_GUID("width", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("height", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("size", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_PTR("data", NMO_FIELD_REPEATED)
    };
    static const nmo_struct_type_def_t mipmaplevel_def =
        NMO_STRUCT_DEF("MipmapLevel", NMO_GUID_STRUCT_MIPMAPLEVEL, mipmaplevel_fields);

    /* CKTextureReaderSlot */
    static const nmo_struct_field_def_t cktexturereaderslot_fields[] = {
        NMO_STRUCT_FIELD_GUID("format_type", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("extension", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("reader_guid", CKPGUID_GUID),
        NMO_STRUCT_FIELD_GUID("data_size", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_PTR("data", NMO_FIELD_REPEATED),
        NMO_STRUCT_FIELD_GUID("alpha_count", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("alpha_value", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("alpha_plane_size", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_PTR("alpha_plane", NMO_FIELD_REPEATED)
    };
    static const nmo_struct_type_def_t cktexturereaderslot_def =
        NMO_STRUCT_DEF("CKTextureReaderSlot", NMO_GUID_STRUCT_CKTEXTUREREADERSLOT, cktexturereaderslot_fields);

    /* CKTextureRawSlot */
    static const nmo_struct_field_def_t cktexturerawslot_fields[] = {
        NMO_STRUCT_FIELD_GUID("bits_per_pixel", CKPGUID_INT),
        NMO_STRUCT_FIELD_GUID("width", CKPGUID_INT),
        NMO_STRUCT_FIELD_GUID("height", CKPGUID_INT),
        NMO_STRUCT_FIELD_GUID("alpha_mask", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("red_mask", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("green_mask", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("blue_mask", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("compression", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("blue_size", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_PTR("blue_data", NMO_FIELD_REPEATED),
        NMO_STRUCT_FIELD_GUID("green_size", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_PTR("green_data", NMO_FIELD_REPEATED),
        NMO_STRUCT_FIELD_GUID("red_size", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_PTR("red_data", NMO_FIELD_REPEATED),
        NMO_STRUCT_FIELD_GUID("alpha_size", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_PTR("alpha_data", NMO_FIELD_REPEATED)
    };
    static const nmo_struct_type_def_t cktexturerawslot_def =
        NMO_STRUCT_DEF("CKTextureRawSlot", NMO_GUID_STRUCT_CKTEXTURERAWSLOT, cktexturerawslot_fields);

    /* CKTextureBitmap2Slot */
    static const nmo_struct_field_def_t cktexturebitmap2slot_fields[] = {
        NMO_STRUCT_FIELD_GUID("header_size", CKPGUID_INT),
        NMO_STRUCT_FIELD_GUID("buffer_size", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_PTR("buffer", NMO_FIELD_REPEATED)
    };
    static const nmo_struct_type_def_t cktexturebitmap2slot_def =
        NMO_STRUCT_DEF("CKTextureBitmap2Slot", NMO_GUID_STRUCT_CKTEXTUREBITMAP2SLOT, cktexturebitmap2slot_fields);

    /* MaterialColors */
    static const nmo_struct_field_def_t materialcolors_fields[] = {
        NMO_STRUCT_FIELD_GUID("ambient", CKPGUID_COLORF),
        NMO_STRUCT_FIELD_GUID("diffuse", CKPGUID_COLORF),
        NMO_STRUCT_FIELD_GUID("specular", CKPGUID_COLORF),
        NMO_STRUCT_FIELD_GUID("emissive", CKPGUID_COLORF)
    };
    static const nmo_struct_type_def_t materialcolors_def =
        NMO_STRUCT_DEF("MaterialColors", NMO_GUID_STRUCT_MATERIALCOLORS, materialcolors_fields);

    /* CKAttributeCategory */
    static const nmo_struct_field_def_t ckattributecategory_fields[] = {
        NMO_STRUCT_FIELD_GUID("name", CKPGUID_STRING),
        NMO_STRUCT_FIELD_GUID("flags", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("present", CKPGUID_BOOL)
    };
    static const nmo_struct_type_def_t ckattributecategory_def =
        NMO_STRUCT_DEF("CKAttributeCategory", NMO_GUID_STRUCT_CKATTRIBUTECATEGORY, ckattributecategory_fields);

    /* CKAttributeDescriptor */
    static const nmo_struct_field_def_t ckattributedescriptor_fields[] = {
        NMO_STRUCT_FIELD_GUID("name", CKPGUID_STRING),
        NMO_STRUCT_FIELD_GUID("parameter_type_guid", CKPGUID_GUID),
        NMO_STRUCT_FIELD_GUID("category_index", CKPGUID_INT),
        NMO_STRUCT_FIELD_GUID("compatible_class_id", CKPGUID_INT),
        NMO_STRUCT_FIELD_GUID("flags", CKPGUID_UINT32),
        NMO_STRUCT_FIELD_GUID("present", CKPGUID_BOOL)
    };
    static const nmo_struct_type_def_t ckattributedescriptor_def =
        NMO_STRUCT_DEF("CKAttributeDescriptor", NMO_GUID_STRUCT_CKATTRIBUTEDESCRIPTOR, ckattributedescriptor_fields);

    /* CKKeyedAnimationSubanim */
    static const nmo_struct_field_def_t ckkeyedanimationsubanim_fields[] = {
        NMO_STRUCT_FIELD_GUID_FLAGS("object_id", CKPGUID_ID, NMO_FIELD_REFERENCE),
        NMO_STRUCT_FIELD_GUID("chunk", CKPGUID_STATECHUNK)
    };
    static const nmo_struct_type_def_t ckkeyedanimationsubanim_def =
        NMO_STRUCT_DEF("CKKeyedAnimationSubanim", NMO_GUID_STRUCT_CKKEYEDANIMATIONSUBANIM,
                       ckkeyedanimationsubanim_fields);

    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &colorf_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &ckbitmapdata_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &fontinfo_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &cklightdata_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &ck3dentityskinvertex_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &ck3dentityskinbone_def, NULL));
    NMO_RETURN_IF_ERROR(nmo_type_registry_register_struct(registry, &ck3dentityskin_def, NULL));
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

