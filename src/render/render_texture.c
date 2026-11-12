/**
 * @file render_texture.c
 * @brief CKTextures as the backend has them: uploaded when their image
 *        changes, with the mipmaps CK would make (RCKTexture::SystemToVideoMemory).
 */

#include "render_internal.h"
#include "render_image.h"

#include <stdlib.h>
#include <string.h>

render_texture_t *render_texture_entry(nmo_render_t *render, nmo_object_id_t id)
{
    for (size_t i = 0; i < render->texture_count; i++) {
        if (render->textures[i].id == id) {
            return &render->textures[i];
        }
    }
    return NULL;
}

/* The levels below the image: its file's own (CKTexture's user mipmaps), else made from it
   down to 1 x 1; how many there are */
static int texture_upload_mipmaps(nmo_render_t *render, uintptr_t handle, nmo_object_id_t id,
                                  const nmo_engine_image_t *image)
{
    render_backend_t *backend = render->backend;
    int saved = nmo_engine_texture_user_mipmap_count(render->engine, id);
    if (saved > 0) {
        int level = 0;
        nmo_engine_image_t mip;
        while (level < saved && nmo_engine_texture_user_mipmap(render->engine, id, level + 1, &mip)) {
            level++;
            backend->ops->texture_image(backend, handle, RENDER_TEXTURE_2D, 0, level, mip.width,
                                        mip.height, mip.rgba);
        }
        return level;
    }
    int w = image->width, h = image->height, level = 0;
    const uint8_t *source = image->rgba;
    bool binary = render_image_is_binary(source, w, h);
    uint8_t *made = NULL;
    while (w > 1 || h > 1) {
        int next_w = 0, next_h = 0;
        uint8_t *half = render_half_image(source, w, h, binary, &next_w, &next_h);
        free(made);
        if (half == NULL) {
            break;
        }
        made = half;
        source = half;
        w = next_w;
        h = next_h;
        level++;
        backend->ops->texture_image(backend, handle, RENDER_TEXTURE_2D, 0, level, w, h, half);
    }
    free(made);
    return level;
}

uintptr_t render_texture(nmo_render_t *render, nmo_object_id_t id)
{
    if (id == 0) {
        return 0;
    }
    render_backend_t *backend = render->backend;
    render_texture_t *texture = render_texture_entry(render, id);
    if (texture != NULL && texture->target) {
        return texture->handle;
    }
    /* A cube map's faces, its version the newest of theirs */
    bool cube = nmo_engine_texture_cube_map(render->engine, id);
    nmo_engine_image_t faces[6];
    nmo_engine_image_t image;
    memset(&image, 0, sizeof(image));
    bool has_image = false;
    if (cube) {
        has_image = true;
        image.version = 0;
        for (int32_t face = 0; face < 6 && has_image; face++) {
            has_image = nmo_engine_texture_face_image(render->engine, id, face, &faces[face]);
            image.version = faces[face].version > image.version ? faces[face].version : image.version;
        }
    } else {
        has_image = nmo_engine_texture_image(render->engine, id, &image);
    }
    if (texture == NULL) {
        if (render->texture_count == render->texture_capacity) {
            size_t capacity = render->texture_capacity != 0 ? render->texture_capacity * 2 : 64;
            render_texture_t *grown =
                (render_texture_t *)realloc(render->textures, capacity * sizeof(*grown));
            if (grown == NULL) {
                return 0;
            }
            render->textures = grown;
            render->texture_capacity = capacity;
        }
        texture = &render->textures[render->texture_count++];
        memset(texture, 0, sizeof(*texture));
        texture->id = id;
        texture->handle = backend->ops->texture_create(backend);
        texture->version = has_image ? image.version - 1 : 0;
        texture->empty = true;
    }
    if (texture->handle == 0) {
        return 0;
    }
    /* Forced mipmaps leave out cutouts (clear texels, a transparent color's): small sprites
       of them would gather their dark edges into rings, which CK draws without */
    if (cube) {
        if (has_image && image.version != texture->version) {
            texture->version = image.version;
            texture->empty = false;
            texture->cube = true;
            texture->mipmapped = texture->forced = false;
            for (int32_t face = 0; face < 6; face++) {
                backend->ops->texture_image(backend, texture->handle, RENDER_TEXTURE_CUBE, face, 0,
                                            faces[face].width, faces[face].height, faces[face].rgba);
            }
            backend->ops->texture_levels(backend, texture->handle, RENDER_TEXTURE_CUBE, 0);
        }
        return texture->empty ? 0 : texture->handle;
    }
    bool mipmapped = has_image && (image.mipmapped ||
                                   (render->mipmap_all && !render_image_has_clear_texels(&image)));
    if (has_image && (image.version != texture->version || mipmapped != texture->mipmapped)) {
        texture->version = image.version;
        texture->empty = false;
        texture->mipmapped = mipmapped;
        texture->forced = mipmapped && !image.mipmapped;
        backend->ops->texture_image(backend, texture->handle, RENDER_TEXTURE_2D, 0, 0, image.width,
                                    image.height, image.rgba);
        /* RCKTexture::SystemToVideoMemory: mipmaps only for a texture that uses them */
        int levels = mipmapped ? texture_upload_mipmaps(render, texture->handle, id, &image) : 0;
        backend->ops->texture_levels(backend, texture->handle, RENDER_TEXTURE_2D, levels);
    }
    return texture->empty ? 0 : texture->handle;
}

void render_textures_free(nmo_render_t *render)
{
    for (size_t i = 0; i < render->texture_count; i++) {
        if (render->textures[i].handle != 0) {
            render->backend->ops->texture_destroy(render->backend, render->textures[i].handle);
        }
    }
    free(render->textures);
    render->textures = NULL;
    render->texture_count = render->texture_capacity = 0;
}
