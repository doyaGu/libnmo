/**
 * @file nmo_list.h
 * @brief Arena-backed doubly linked list container.
 *
 * The list allocates nodes from an arena, providing fast allocation and
 * predictable lifetimes. Removed nodes are kept on an internal free-list so
 * they can be reused without further arena growth.
 */

#ifndef NMO_LIST_H
#define NMO_LIST_H

#include "nmo_types.h"
#include "core/nmo_arena.h"
#include "core/nmo_container_lifecycle.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_list nmo_list_t;
typedef struct nmo_list_node nmo_list_node_t;

/**
 * @brief Create a new arena-backed list.
 *
 * @param arena Arena for node allocations (NULL to create a private arena).
 * @param element_size Size of each stored element in bytes.
 * @return New list or NULL on allocation failure.
 */
nmo_list_t *nmo_list_create(nmo_arena_t *arena, size_t element_size);

/**
 * @brief Destroy a list, running lifecycle callbacks for live nodes.
 */
void nmo_list_destroy(nmo_list_t *list);

/**
 * @brief Configure lifecycle hooks for stored elements.
 *
 * @param list List to configure.
 * @param lifecycle Lifecycle callbacks (NULL to clear).
 */
void nmo_list_set_lifecycle(nmo_list_t *list,
                            const nmo_container_lifecycle_t *lifecycle);

/**
 * @brief Push an element at the back of the list.
 *
 * @param list List to modify.
 * @param element Pointer to element data.
 * @return Node handle or NULL on failure.
 */
nmo_list_node_t *nmo_list_push_back(nmo_list_t *list, const void *element);

/**
 * @brief Push an element at the front of the list.
 */
nmo_list_node_t *nmo_list_push_front(nmo_list_t *list, const void *element);

/**
 * @brief Insert an element after the specified node (or at front if node is NULL).
 */
nmo_list_node_t *nmo_list_insert_after(nmo_list_t *list,
                                       nmo_list_node_t *node,
                                       const void *element);

/**
 * @brief Insert an element before the specified node (or at back if node is NULL).
 */
nmo_list_node_t *nmo_list_insert_before(nmo_list_t *list,
                                        nmo_list_node_t *node,
                                        const void *element);

/**
 * @brief Pop the element at the back of the list.
 *
 * @param list List to modify.
 * @param out_element Optional pointer receiving the removed value.
 * @return 1 on success, 0 if list is empty.
 */
int nmo_list_pop_back(nmo_list_t *list, void *out_element);

/**
 * @brief Pop the element at the front of the list.
 */
int nmo_list_pop_front(nmo_list_t *list, void *out_element);

/**
 * @brief Remove a node from the list.
 *
 * @param list List to modify.
 * @param node Node to remove (must belong to list).
 * @param out_element Optional pointer receiving removed data.
 */
void nmo_list_remove(nmo_list_t *list, nmo_list_node_t *node, void *out_element);

/**
 * @brief Clear all nodes from the list.
 *
 * Nodes are recycled for future insertions, keeping allocations amortized.
 */
void nmo_list_clear(nmo_list_t *list);

/**
 * @brief Get the number of elements.
 */
size_t nmo_list_get_count(const nmo_list_t *list);

/**
 * @brief Check if list is empty.
 */
int nmo_list_is_empty(const nmo_list_t *list);

/**
 * @brief Accessors for iterating over the list.
 */
nmo_list_node_t *nmo_list_begin(const nmo_list_t *list);
nmo_list_node_t *nmo_list_end(const nmo_list_t *list);
nmo_list_node_t *nmo_list_next(const nmo_list_node_t *node);
nmo_list_node_t *nmo_list_prev(const nmo_list_node_t *node);

/**
 * @brief Get a mutable pointer to the element stored in a node.
 */
void *nmo_list_node_get(nmo_list_node_t *node);

/**
 * @brief Get a read-only pointer to the element stored in a node.
 */
const void *nmo_list_node_get_const(const nmo_list_node_t *node);

#ifdef __cplusplus
}
#endif

#endif /* NMO_LIST_H */
