/**
 * @file nmo_save_buffer.h
 * @brief Memory buffer for two-phase save pipeline
 *
 * Provides a growable memory buffer for serializing data before writing to file.
 * Used internally by the save pipeline to hold Header1 and Data sections.
 */

#ifndef NMO_SAVE_BUFFER_H
#define NMO_SAVE_BUFFER_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque save buffer structure
 */
typedef struct nmo_save_buffer nmo_save_buffer_t;

/**
 * @brief Create a save buffer with initial capacity
 *
 * @param arena Arena for allocations (buffer data uses malloc for resizing)
 * @param initial_capacity Initial capacity in bytes (0 for default)
 * @return New buffer, or NULL on error
 * @ownership owned
 */
NMO_API nmo_save_buffer_t *nmo_save_buffer_create(nmo_arena_t *arena, size_t initial_capacity);

/**
 * @brief Destroy save buffer and free memory
 *
 * @param buffer Buffer to destroy
 */
NMO_API void nmo_save_buffer_destroy(nmo_save_buffer_t *buffer);

/**
 * @brief Reset buffer to empty state (keeps capacity)
 *
 * @param buffer Buffer to reset
 */
NMO_API void nmo_save_buffer_reset(nmo_save_buffer_t *buffer);

/**
 * @brief Get current write position (size of written data)
 *
 * @param buffer Buffer
 * @return Current size in bytes
 */
NMO_API size_t nmo_save_buffer_size(const nmo_save_buffer_t *buffer);

/**
 * @brief Get current capacity
 *
 * @param buffer Buffer
 * @return Current capacity in bytes
 */
NMO_API size_t nmo_save_buffer_capacity(const nmo_save_buffer_t *buffer);

/**
 * @brief Get pointer to buffer data
 *
 * @param buffer Buffer
 * @return Pointer to data (valid until next write/resize)
 * @ownership borrowed
 */
NMO_API void *nmo_save_buffer_data(nmo_save_buffer_t *buffer);

/**
 * @brief Get const pointer to buffer data
 *
 * @param buffer Buffer
 * @return Const pointer to data
 * @ownership borrowed
 */
NMO_API const void *nmo_save_buffer_data_const(const nmo_save_buffer_t *buffer);

/**
 * @brief Ensure buffer has at least specified capacity
 *
 * @param buffer Buffer
 * @param capacity Required capacity in bytes
 * @return NMO_OK on success, NMO_ERR_NOMEM on allocation failure
 */
NMO_API int nmo_save_buffer_reserve(nmo_save_buffer_t *buffer, size_t capacity);

/**
 * @brief Write bytes to buffer at current position
 *
 * @param buffer Buffer
 * @param data Data to write
 * @param size Number of bytes to write
 * @return NMO_OK on success, error code on failure
 */
NMO_API int nmo_save_buffer_write(nmo_save_buffer_t *buffer, const void *data, size_t size);

/**
 * @brief Write a uint32_t to buffer
 *
 * @param buffer Buffer
 * @param value Value to write
 * @return NMO_OK on success
 */
NMO_API int nmo_save_buffer_write_u32(nmo_save_buffer_t *buffer, uint32_t value);

/**
 * @brief Write a uint64_t to buffer
 *
 * @param buffer Buffer
 * @param value Value to write
 * @return NMO_OK on success
 */
NMO_API int nmo_save_buffer_write_u64(nmo_save_buffer_t *buffer, uint64_t value);

/**
 * @brief Reserve space at current position and return offset
 *
 * Used for forward references - reserve space, write data later, then patch.
 *
 * @param buffer Buffer
 * @param size Number of bytes to reserve
 * @param out_offset Output: offset of reserved space
 * @return NMO_OK on success
 */
NMO_API int nmo_save_buffer_reserve_space(nmo_save_buffer_t *buffer, size_t size, size_t *out_offset);

/**
 * @brief Patch data at a specific offset
 *
 * Used to fill in reserved space after knowing the actual value.
 *
 * @param buffer Buffer
 * @param offset Offset to patch at
 * @param data Data to write
 * @param size Number of bytes to write
 * @return NMO_OK on success, NMO_ERR_INVALID_ARGUMENT if offset is invalid
 */
NMO_API int nmo_save_buffer_patch(nmo_save_buffer_t *buffer, size_t offset, const void *data, size_t size);

/**
 * @brief Patch a uint32_t at a specific offset
 *
 * @param buffer Buffer
 * @param offset Offset to patch at
 * @param value Value to write
 * @return NMO_OK on success
 */
NMO_API int nmo_save_buffer_patch_u32(nmo_save_buffer_t *buffer, size_t offset, uint32_t value);

/**
 * @brief Transfer ownership of buffer data to caller
 *
 * After this call, the buffer is reset and the returned pointer
 * must be freed by the caller using free().
 *
 * @param buffer Buffer
 * @param out_size Output: size of returned data
 * @return Pointer to data (caller owns), or NULL if empty
 * @ownership owned
 */
NMO_API void *nmo_save_buffer_detach(nmo_save_buffer_t *buffer, size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SAVE_BUFFER_H */
