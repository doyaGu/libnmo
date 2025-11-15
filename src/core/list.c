/**
 * @file list.c
 * @brief Arena-backed doubly linked list implementation.
 */

#include "core/nmo_list.h"
#include "core/nmo_error.h"
#include <stdlib.h>
#include <string.h>

struct nmo_list_node {
    struct nmo_list_node *next;
    struct nmo_list_node *prev;
    uint8_t data[];
};

struct nmo_list {
    nmo_arena_t *arena;
    int owns_arena;
    size_t element_size;
    size_t count;
    nmo_list_node_t *head;
    nmo_list_node_t *tail;
    nmo_list_node_t *free_nodes;
    nmo_container_lifecycle_t lifecycle;
};

static size_t nmo_list_alignment(size_t size) {
    size_t alignment = size < sizeof(void*) ? sizeof(void*) : size;
    if (alignment & (alignment - 1)) {
        alignment = sizeof(void*);
    }
    return alignment;
}

static nmo_list_node_t *nmo_list_acquire_node(nmo_list_t *list) {
    nmo_list_node_t *node = NULL;
    if (list->free_nodes) {
        node = list->free_nodes;
        list->free_nodes = node->next;
    } else {
        size_t alloc_size = sizeof(nmo_list_node_t) + list->element_size;
        node = (nmo_list_node_t *)nmo_arena_alloc(
            list->arena,
            alloc_size,
            nmo_list_alignment(sizeof(nmo_list_node_t)));
    }
    if (node) {
        node->next = NULL;
        node->prev = NULL;
    }
    return node;
}

static void nmo_list_release_node(nmo_list_t *list, nmo_list_node_t *node) {
    node->next = list->free_nodes;
    node->prev = NULL;
    list->free_nodes = node;
}

static void nmo_list_run_dispose(nmo_list_t *list, nmo_list_node_t *node) {
    if (list->lifecycle.dispose) {
        list->lifecycle.dispose(node->data, list->lifecycle.user_data);
    }
}

static void nmo_list_attach_front(nmo_list_t *list, nmo_list_node_t *node) {
    node->next = list->head;
    node->prev = NULL;
    if (list->head) {
        list->head->prev = node;
    } else {
        list->tail = node;
    }
    list->head = node;
    list->count++;
}

static void nmo_list_attach_back(nmo_list_t *list, nmo_list_node_t *node) {
    node->prev = list->tail;
    node->next = NULL;
    if (list->tail) {
        list->tail->next = node;
    } else {
        list->head = node;
    }
    list->tail = node;
    list->count++;
}

static void nmo_list_attach_after(nmo_list_t *list,
                                  nmo_list_node_t *pos,
                                  nmo_list_node_t *node) {
    if (pos == NULL) {
        nmo_list_attach_front(list, node);
        return;
    }
    node->prev = pos;
    node->next = pos->next;
    if (pos->next) {
        pos->next->prev = node;
    } else {
        list->tail = node;
    }
    pos->next = node;
    list->count++;
}

static void nmo_list_attach_before(nmo_list_t *list,
                                   nmo_list_node_t *pos,
                                   nmo_list_node_t *node) {
    if (pos == NULL) {
        nmo_list_attach_back(list, node);
        return;
    }
    node->next = pos;
    node->prev = pos->prev;
    if (pos->prev) {
        pos->prev->next = node;
    } else {
        list->head = node;
    }
    pos->prev = node;
    list->count++;
}

static void nmo_list_detach(nmo_list_t *list, nmo_list_node_t *node) {
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        list->head = node->next;
    }
    if (node->next) {
        node->next->prev = node->prev;
    } else {
        list->tail = node->prev;
    }
    if (list->count > 0) {
        list->count--;
    }
    node->next = NULL;
    node->prev = NULL;
}

nmo_list_t *nmo_list_create(nmo_arena_t *arena, size_t element_size) {
    if (element_size == 0) {
        return NULL;
    }

    nmo_list_t *list = (nmo_list_t *)malloc(sizeof(nmo_list_t));
    if (!list) {
        return NULL;
    }

    nmo_arena_t *arena_to_use = arena;
    int owns_arena = 0;
    if (!arena_to_use) {
        arena_to_use = nmo_arena_create(NULL, 0);
        if (!arena_to_use) {
            free(list);
            return NULL;
        }
        owns_arena = 1;
    }

    list->arena = arena_to_use;
    list->owns_arena = owns_arena;
    list->element_size = element_size;
    list->count = 0;
    list->head = NULL;
    list->tail = NULL;
    list->free_nodes = NULL;
    list->lifecycle.dispose = NULL;
    list->lifecycle.user_data = NULL;

    return list;
}

void nmo_list_destroy(nmo_list_t *list) {
    if (!list) {
        return;
    }

    nmo_list_clear(list);

    if (list->owns_arena && list->arena) {
        nmo_arena_destroy(list->arena);
    }

    free(list);
}

void nmo_list_set_lifecycle(nmo_list_t *list,
                            const nmo_container_lifecycle_t *lifecycle) {
    if (!list) {
        return;
    }

    if (lifecycle) {
        list->lifecycle = *lifecycle;
    } else {
        list->lifecycle.dispose = NULL;
        list->lifecycle.user_data = NULL;
    }
}

nmo_list_node_t *nmo_list_push_back(nmo_list_t *list, const void *element) {
    if (!list || !element) {
        return NULL;
    }

    nmo_list_node_t *node = nmo_list_acquire_node(list);
    if (!node) {
        return NULL;
    }

    memcpy(node->data, element, list->element_size);
    nmo_list_attach_back(list, node);
    return node;
}

nmo_list_node_t *nmo_list_push_front(nmo_list_t *list, const void *element) {
    if (!list || !element) {
        return NULL;
    }

    nmo_list_node_t *node = nmo_list_acquire_node(list);
    if (!node) {
        return NULL;
    }
    memcpy(node->data, element, list->element_size);
    nmo_list_attach_front(list, node);
    return node;
}

nmo_list_node_t *nmo_list_insert_after(nmo_list_t *list,
                                       nmo_list_node_t *pos,
                                       const void *element) {
    if (!list || !element) {
        return NULL;
    }

    nmo_list_node_t *node = nmo_list_acquire_node(list);
    if (!node) {
        return NULL;
    }
    memcpy(node->data, element, list->element_size);
    nmo_list_attach_after(list, pos, node);
    return node;
}

nmo_list_node_t *nmo_list_insert_before(nmo_list_t *list,
                                        nmo_list_node_t *pos,
                                        const void *element) {
    if (!list || !element) {
        return NULL;
    }

    nmo_list_node_t *node = nmo_list_acquire_node(list);
    if (!node) {
        return NULL;
    }
    memcpy(node->data, element, list->element_size);
    nmo_list_attach_before(list, pos, node);
    return node;
}

int nmo_list_pop_back(nmo_list_t *list, void *out_element) {
    if (!list || !list->tail) {
        return 0;
    }

    nmo_list_node_t *node = list->tail;
    if (out_element) {
        memcpy(out_element, node->data, list->element_size);
    }
    nmo_list_run_dispose(list, node);
    nmo_list_detach(list, node);
    nmo_list_release_node(list, node);
    return 1;
}

int nmo_list_pop_front(nmo_list_t *list, void *out_element) {
    if (!list || !list->head) {
        return 0;
    }

    nmo_list_node_t *node = list->head;
    if (out_element) {
        memcpy(out_element, node->data, list->element_size);
    }
    nmo_list_run_dispose(list, node);
    nmo_list_detach(list, node);
    nmo_list_release_node(list, node);
    return 1;
}

void nmo_list_remove(nmo_list_t *list, nmo_list_node_t *node, void *out_element) {
    if (!list || !node) {
        return;
    }

    if (out_element) {
        memcpy(out_element, node->data, list->element_size);
    }
    nmo_list_run_dispose(list, node);
    nmo_list_detach(list, node);
    nmo_list_release_node(list, node);
}

void nmo_list_clear(nmo_list_t *list) {
    if (!list) {
        return;
    }

    nmo_list_node_t *node = list->head;
    while (node) {
        nmo_list_node_t *next = node->next;
        nmo_list_run_dispose(list, node);
        nmo_list_release_node(list, node);
        node = next;
    }
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
}

size_t nmo_list_get_count(const nmo_list_t *list) {
    return list ? list->count : 0;
}

int nmo_list_is_empty(const nmo_list_t *list) {
    return list ? (list->count == 0) : 1;
}

nmo_list_node_t *nmo_list_begin(const nmo_list_t *list) {
    return list ? list->head : NULL;
}

nmo_list_node_t *nmo_list_end(const nmo_list_t *list) {
    (void)list;
    return NULL;
}

nmo_list_node_t *nmo_list_next(const nmo_list_node_t *node) {
    return node ? node->next : NULL;
}

nmo_list_node_t *nmo_list_prev(const nmo_list_node_t *node) {
    return node ? node->prev : NULL;
}

void *nmo_list_node_get(nmo_list_node_t *node) {
    return node ? node->data : NULL;
}

const void *nmo_list_node_get_const(const nmo_list_node_t *node) {
    return node ? node->data : NULL;
}
