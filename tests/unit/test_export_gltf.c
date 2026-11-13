/**
 * @file test_export_gltf.c
 * @brief The 3D scene of a file as glTF binary
 */

#include "../test_framework.h"

#include "document/nmo_document_export.h"
#include "document/nmo_document_load.h"
#include "export/nmo_export_gltf.h"
#include "format/nmo_object.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_context.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"
#include "yyjson.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* A .glb whose header and chunks check out */
typedef struct glb {
    uint8_t *bytes;
    yyjson_doc *doc;
    const uint8_t *bin;
    size_t bin_size;
} glb_t;

static bool glb_open(const char *path, glb_t *out)
{
    memset(out, 0, sizeof(*out));
    FILE *file = fopen(path, "rb");
    if (file == NULL) return false;
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    out->bytes = size > 20 ? (uint8_t *)malloc((size_t)size) : NULL;
    const bool read = out->bytes != NULL && fread(out->bytes, 1, (size_t)size, file) == (size_t)size;
    fclose(file);
    if (!read) return false;
    const uint8_t *b = out->bytes;
    const uint32_t json_size = read_u32(b + 12);
    if (read_u32(b) != 0x46546C67u || read_u32(b + 4) != 2u || read_u32(b + 8) != (uint32_t)size ||
        read_u32(b + 16) != 0x4E4F534Au || json_size % 4u != 0u || 20u + json_size > (size_t)size) {
        return false;
    }
    out->doc = yyjson_read((const char *)b + 20, json_size, 0);
    const size_t at = 20u + json_size;
    if (at < (size_t)size) {
        if (at + 8u > (size_t)size || read_u32(b + at + 4) != 0x004E4942u ||
            at + 8u + read_u32(b + at) != (size_t)size) {
            return false;
        }
        out->bin = b + at + 8u;
        out->bin_size = read_u32(b + at);
    }
    return out->doc != NULL;
}

static void glb_close(glb_t *glb)
{
    yyjson_doc_free(glb->doc);
    free(glb->bytes);
}

static size_t arr_size(yyjson_val *root, const char *key)
{
    return yyjson_arr_size(yyjson_obj_get(root, key));
}

static uint64_t get_uint(yyjson_val *obj, const char *key)
{
    return yyjson_get_uint(yyjson_obj_get(obj, key));
}

/* Column-major product, as glTF composes a node's matrix with its parent's */
static void mat4_multiply(const float a[16], const float b[16], float out[16])
{
    float r[16];
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a[k * 4 + row] * b[c * 4 + k];
            r[c * 4 + row] = sum;
        }
    }
    memcpy(out, r, sizeof(r));
}

static void node_matrix(yyjson_val *node, float out[16])
{
    static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    memcpy(out, identity, sizeof(identity));
    yyjson_val *matrix = yyjson_obj_get(node, "matrix");
    for (size_t i = 0; i < 16 && i < yyjson_arr_size(matrix); ++i) {
        out[i] = (float)yyjson_get_num(yyjson_arr_get(matrix, i));
    }
}

/* Every view inside the binary chunk, every accessor inside its view, every
   index below its vertex count and every embedded image a PNG */
static bool binary_layout_holds(const glb_t *glb, yyjson_val *root)
{
    yyjson_val *views = yyjson_obj_get(root, "bufferViews");
    yyjson_val *accessors = yyjson_obj_get(root, "accessors");
    size_t i, max;
    yyjson_val *item;
    yyjson_arr_foreach(views, i, max, item) {
        const uint64_t offset = get_uint(item, "byteOffset");
        if (offset % 4u != 0u || offset + get_uint(item, "byteLength") > glb->bin_size) return false;
    }
    yyjson_arr_foreach(accessors, i, max, item) {
        yyjson_val *view = yyjson_arr_get(views, get_uint(item, "bufferView"));
        const char *type = yyjson_get_str(yyjson_obj_get(item, "type"));
        const uint64_t component = get_uint(item, "componentType");
        const uint64_t components = strcmp(type, "SCALAR") == 0 ? 1 : (uint64_t)(type[3] - '0');
        const uint64_t bytes = component == 5126 ? 4u : (component == 5123 ? 2u : 1u);
        if (view == NULL || get_uint(item, "count") * components * bytes > get_uint(view, "byteLength")) {
            return false;
        }
    }
    yyjson_val *mesh;
    size_t mi, mesh_max;
    yyjson_arr_foreach(yyjson_obj_get(root, "meshes"), mi, mesh_max, mesh) {
        yyjson_val *primitive;
        size_t pi, primitive_max;
        yyjson_arr_foreach(yyjson_obj_get(mesh, "primitives"), pi, primitive_max, primitive) {
            yyjson_val *position = yyjson_arr_get(
                accessors, get_uint(yyjson_obj_get(primitive, "attributes"), "POSITION"));
            yyjson_val *indices = yyjson_arr_get(accessors, get_uint(primitive, "indices"));
            yyjson_val *view = yyjson_arr_get(views, get_uint(indices, "bufferView"));
            if (yyjson_obj_get(position, "min") == NULL || get_uint(indices, "count") % 3u != 0u) {
                return false;
            }
            const uint8_t *data = glb->bin + get_uint(view, "byteOffset");
            for (uint64_t k = 0; k < get_uint(indices, "count"); ++k) {
                const uint16_t index = (uint16_t)(data[k * 2u] | (data[k * 2u + 1u] << 8));
                if (index >= get_uint(position, "count")) return false;
            }
        }
    }
    yyjson_arr_foreach(yyjson_obj_get(root, "images"), i, max, item) {
        yyjson_val *view_index = yyjson_obj_get(item, "bufferView");
        if (view_index == NULL) continue;
        yyjson_val *view = yyjson_arr_get(views, yyjson_get_uint(view_index));
        if (memcmp(glb->bin + get_uint(view, "byteOffset"), "\x89PNG\r\n\x1a\n", 8) != 0) return false;
    }
    return true;
}

TEST(export_gltf, writes_the_entities_meshes_and_embedded_textures)
{
    const char *path = "data/Demo/BTE.cmo";
    TEST_REQUIRE_FILE(path);
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = NMO_TEST_DATA_DIR });
    ASSERT_NOT_NULL(ctx);
    nmo_document_t *document = NULL;
    ASSERT_EQ(NMO_OK, nmo_document_load_file(ctx, path, NULL, &document));
    const char *out = NMO_TEST_SCRATCH_FILE("bte.glb");
    nmo_gltf_export_stats_t stats;
    ASSERT_EQ(NMO_OK, nmo_document_export_gltf(document, out, NULL, &stats));
    ASSERT_TRUE(stats.nodes > 0u);
    ASSERT_TRUE(stats.meshes > 0u);
    ASSERT_TRUE(stats.triangles > 0u);
    ASSERT_TRUE(stats.images_embedded > 0u);

    glb_t glb;
    ASSERT_TRUE(glb_open(out, &glb));
    yyjson_val *root = yyjson_doc_get_root(glb.doc);
    ASSERT_STR_EQ("2.0", yyjson_get_str(yyjson_obj_get(yyjson_obj_get(root, "asset"), "version")));
    ASSERT_EQ(stats.nodes, arr_size(root, "nodes"));
    ASSERT_EQ(stats.meshes, arr_size(root, "meshes"));
    ASSERT_EQ(stats.materials, arr_size(root, "materials"));
    ASSERT_EQ(stats.images_embedded + stats.images_external, arr_size(root, "images"));
    ASSERT_TRUE(binary_layout_holds(&glb, root));

    /* Node i is the i-th 3D entity; its matrix composed with its parents'
       is the entity's world matrix with Z negated */
    yyjson_val *nodes = yyjson_obj_get(root, "nodes");
    const size_t node_count = yyjson_arr_size(nodes);
    int64_t *parent = (int64_t *)malloc(node_count * sizeof(int64_t));
    ASSERT_NOT_NULL(parent);
    for (size_t i = 0; i < node_count; ++i) parent[i] = -1;
    size_t with_parent = 0;
    for (size_t i = 0; i < node_count; ++i) {
        yyjson_val *child;
        size_t ci, child_max;
        yyjson_arr_foreach(yyjson_obj_get(yyjson_arr_get(nodes, i), "children"), ci, child_max, child) {
            parent[yyjson_get_uint(child)] = (int64_t)i;
            with_parent++;
        }
    }
    ASSERT_TRUE(with_parent > 0u);
    const nmo_type_registry_t *types = nmo_context_get_type_registry(ctx);
    nmo_object_repository_t *repo = nmo_document_get_repository(document);
    size_t node = 0;
    for (size_t i = 0; i < nmo_object_repository_get_count(repo); ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        const nmo_3dentity_state_t *state =
            nmo_type_query_object_is_derived_from_class(types, object, NMO_CID_3DENTITY)
                ? (const nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                      types, object, CKPGUID_3DENTITY)
                : NULL;
        if (state == NULL) continue;
        ASSERT_TRUE(node < node_count);
        float world[16];
        node_matrix(yyjson_arr_get(nodes, node), world);
        for (int64_t at = parent[node]; at >= 0; at = parent[at]) {
            float local[16];
            node_matrix(yyjson_arr_get(nodes, (size_t)at), local);
            mat4_multiply(local, world, world);
        }
        for (int c = 0; c < 4; ++c) {
            for (int row = 0; row < 4; ++row) {
                const float sign = ((row == 2) != (c == 2)) ? -1.0f : 1.0f;
                const float expected = sign * state->world_matrix[c * 4 + row];
                ASSERT_TRUE(fabsf(world[c * 4 + row] - expected) <= 1e-3f * fmaxf(1.0f, fabsf(expected)));
            }
        }
        node++;
    }
    ASSERT_EQ(node_count, node);
    free(parent);
    glb_close(&glb);
    remove(out);
    nmo_document_destroy(document);
    nmo_context_release(ctx);
}

TEST(export_gltf, names_the_files_of_textures_it_does_not_embed)
{
    const char *path = "data/Demo/CrashCube.cmo";
    TEST_REQUIRE_FILE(path);
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = NMO_TEST_DATA_DIR });
    ASSERT_NOT_NULL(ctx);
    nmo_document_t *document = NULL;
    ASSERT_EQ(NMO_OK, nmo_document_load_file(ctx, path, NULL, &document));
    const char *out = NMO_TEST_SCRATCH_FILE("crash_cube.glb");
    nmo_gltf_export_options_t options = nmo_gltf_export_options_default();
    options.embed_textures = false;
    nmo_gltf_export_stats_t stats;
    ASSERT_EQ(NMO_OK, nmo_document_export_gltf(document, out, &options, &stats));
    ASSERT_EQ(0u, stats.images_embedded);
    ASSERT_TRUE(stats.images_external > 0u);

    glb_t glb;
    ASSERT_TRUE(glb_open(out, &glb));
    yyjson_val *root = yyjson_doc_get_root(glb.doc);
    ASSERT_TRUE(binary_layout_holds(&glb, root));
    bool escaped = false;
    yyjson_val *image;
    size_t i, max;
    yyjson_arr_foreach(yyjson_obj_get(root, "images"), i, max, image) {
        const char *uri = yyjson_get_str(yyjson_obj_get(image, "uri"));
        ASSERT_NOT_NULL(uri);
        ASSERT_NULL(strchr(uri, ' '));
        ASSERT_NULL(strchr(uri, '/'));
        ASSERT_NULL(strchr(uri, '\\'));
        escaped = escaped || strstr(uri, "%20") != NULL;
    }
    /* "wall beam4.png" */
    ASSERT_TRUE(escaped);
    glb_close(&glb);
    remove(out);
    nmo_document_destroy(document);
    nmo_context_release(ctx);
}

TEST(export_gltf, rejects_missing_arguments)
{
    nmo_gltf_export_stats_t stats;
    memset(&stats, 0xFF, sizeof(stats));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, nmo_export_gltf(NULL, NULL, "x.glb", NULL, &stats));
    ASSERT_EQ(0u, stats.nodes);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, nmo_document_export_gltf(NULL, "x.glb", NULL, NULL));
    const nmo_gltf_export_options_t options = nmo_gltf_export_options_default();
    ASSERT_TRUE(options.embed_textures);
    ASSERT_TRUE(options.apply_color_key);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(export_gltf, writes_the_entities_meshes_and_embedded_textures);
    REGISTER_TEST(export_gltf, names_the_files_of_textures_it_does_not_embed);
    REGISTER_TEST(export_gltf, rejects_missing_arguments);
TEST_MAIN_END()
