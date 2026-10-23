/**
 * @file nmo_engine_block.h
 * @brief Building block functions for the behavior engine.
 */

#ifndef NMO_ENGINE_BLOCK_H
#define NMO_ENGINE_BLOCK_H

#include "engine/nmo_engine.h"

#define NMO_ENGINE_BLOCK_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_ENGINE_BLOCK_API_TIER NMO_API_TIER_ADVANCED_C

/*
 * A building block function gets the block being executed and does what the
 * block's CK function does with its CKBehavior: it reads which inputs are
 * active, deactivates them, reads its input parameters, writes its output
 * parameters and activates outputs, then returns an NMO_ENGINE_BR_* value.
 * Unless the value has NMO_ENGINE_BR_ACTIVATENEXTFRAME set, the block
 * deactivates after it returns.
 *
 * The functions here correspond to the CKBehavior methods of the same name:
 * nmo_engine_block_get_input_value() is GetInputParameterValue(), and so on.
 * Parameter indexes count the block's parameters of that kind.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_engine_block nmo_engine_block_t;

/* CKM_BEHAVIOR* messages a block callback receives. */
#define NMO_ENGINE_MSG_PAUSE 5u
#define NMO_ENGINE_MSG_RESUME 6u
#define NMO_ENGINE_MSG_RESET 9u
#define NMO_ENGINE_MSG_LOAD 11u
#define NMO_ENGINE_MSG_ACTIVATESCRIPT 16u
#define NMO_ENGINE_MSG_DEACTIVATESCRIPT 17u

typedef int (*nmo_engine_block_fn)(nmo_engine_block_t *block);
typedef int (*nmo_engine_block_callback_fn)(nmo_engine_block_t *block, uint32_t message);

typedef struct nmo_engine_block_desc {
    nmo_guid_t guid;                       /* prototype GUID */
    const char *name;
    nmo_engine_block_fn function;
    nmo_engine_block_callback_fn callback; /* optional */
    void *user_data;
} nmo_engine_block_desc_t;

/**
 * Register a building block function, replacing a built-in one for the same
 * GUID. Only before nmo_engine_start(). The name is copied.
 */
NMO_API nmo_status_t nmo_engine_register_block(nmo_engine_t *engine,
                                               const nmo_engine_block_desc_t *desc);

/** True when the engine has a function for prototype `guid`. */
NMO_API bool nmo_engine_has_block(const nmo_engine_t *engine, nmo_guid_t guid);

/* ---- The block being executed ---- */

NMO_API nmo_engine_t *nmo_engine_block_engine(const nmo_engine_block_t *block);
NMO_API nmo_object_id_t nmo_engine_block_id(const nmo_engine_block_t *block);
/** The object whose script holds the block. */
NMO_API nmo_object_id_t nmo_engine_block_owner(const nmo_engine_block_t *block);
/** The building block version the file records (CKBehavior::GetVersion). */
NMO_API uint32_t nmo_engine_block_version(const nmo_engine_block_t *block);
NMO_API float nmo_engine_block_delta_time(const nmo_engine_block_t *block);
NMO_API void *nmo_engine_block_user_data(const nmo_engine_block_t *block);
/** Pick the function this block runs (a load callback choosing by version). */
NMO_API void nmo_engine_block_set_function(nmo_engine_block_t *block, nmo_engine_block_fn fn);
/** A random number in [0, 1), from the engine's seeded generator. */
NMO_API double nmo_engine_block_random(nmo_engine_block_t *block);

/* ---- IOs ---- */

NMO_API int nmo_engine_block_input_count(const nmo_engine_block_t *block);
NMO_API int nmo_engine_block_output_count(const nmo_engine_block_t *block);
NMO_API bool nmo_engine_block_is_input_active(const nmo_engine_block_t *block, int pos);
NMO_API bool nmo_engine_block_is_output_active(const nmo_engine_block_t *block, int pos);
NMO_API void nmo_engine_block_activate_input(nmo_engine_block_t *block, int pos, bool active);
NMO_API void nmo_engine_block_activate_output(nmo_engine_block_t *block, int pos, bool active);

/* ---- Parameters ---- */

NMO_API int nmo_engine_block_input_parameter_count(const nmo_engine_block_t *block);
NMO_API int nmo_engine_block_output_parameter_count(const nmo_engine_block_t *block);
NMO_API int nmo_engine_block_local_parameter_count(const nmo_engine_block_t *block);

/** Type GUID of a parameter; the null GUID when there is none at `pos`. */
NMO_API nmo_guid_t nmo_engine_block_input_parameter_type(const nmo_engine_block_t *block, int pos);
NMO_API nmo_guid_t nmo_engine_block_output_parameter_type(const nmo_engine_block_t *block,
                                                          int pos);
/** Bytes output parameter `pos` holds now (CKParameter::GetDataSize); 0 when there is none. */
NMO_API size_t nmo_engine_block_output_parameter_size(const nmo_engine_block_t *block, int pos);

/**
 * Copy the value input `pos` reads into `buffer`, at most `size` bytes,
 * running the operation its source is the output of. NMO_ERR_NOT_FOUND
 * when the input does not exist or reads nothing; `buffer` is unchanged then.
 */
NMO_API nmo_status_t nmo_engine_block_get_input_value(nmo_engine_block_t *block,
                                                      int pos,
                                                      void *buffer,
                                                      size_t size);
/** The bytes input `pos` reads (GetInputParameterReadDataPtr); NULL if none. */
NMO_API const void *nmo_engine_block_input_data(nmo_engine_block_t *block,
                                                int pos,
                                                size_t *out_size);

NMO_API nmo_status_t nmo_engine_block_get_output_value(const nmo_engine_block_t *block,
                                                       int pos,
                                                       void *buffer,
                                                       size_t size);
/** Write output `pos`; the parameters it is connected to receive the value. */
NMO_API nmo_status_t nmo_engine_block_set_output_value(nmo_engine_block_t *block,
                                                       int pos,
                                                       const void *data,
                                                       size_t size);
/** Copy what input `in_pos` reads into output `out_pos` (CKParameter::CopyValue). */
NMO_API nmo_status_t nmo_engine_block_copy_input_to_output(nmo_engine_block_t *block,
                                                           int in_pos,
                                                           int out_pos);

NMO_API nmo_status_t nmo_engine_block_get_local_value(const nmo_engine_block_t *block,
                                                      int pos,
                                                      void *buffer,
                                                      size_t size);
NMO_API nmo_status_t nmo_engine_block_set_local_value(nmo_engine_block_t *block,
                                                      int pos,
                                                      const void *data,
                                                      size_t size);

/** The object input `pos` holds (GetInputParameterObject); 0 when none. */
NMO_API nmo_object_id_t nmo_engine_block_get_input_object(nmo_engine_block_t *block, int pos);
/**
 * The object the block acts on (CKBehavior::GetTarget): what its target
 * parameter holds, or the owner when it has none.
 */
NMO_API nmo_object_id_t nmo_engine_block_target(nmo_engine_block_t *block);

/* ---- Messages ---- */

/**
 * Wait for message `type` to reach `object`; on arrival output `output_pos`
 * activates (CKMessageManager::RegisterWait). The block waits for messages
 * (CKBEHAVIOR_WAITSFORMESSAGE), which keeps its graph active.
 */
NMO_API nmo_status_t nmo_engine_block_register_wait(nmo_engine_block_t *block,
                                                    int32_t type,
                                                    int output_pos,
                                                    nmo_object_id_t object);
/** Stop waiting for `type` on `output_pos`, or on every output for -1. */
NMO_API void nmo_engine_block_unregister_wait(nmo_engine_block_t *block,
                                              int32_t type,
                                              int output_pos);
NMO_API nmo_status_t nmo_engine_block_send_message(nmo_engine_block_t *block,
                                                   int32_t type,
                                                   nmo_object_id_t recipient,
                                                   nmo_object_id_t sender);
NMO_API nmo_status_t nmo_engine_block_broadcast_message(nmo_engine_block_t *block,
                                                        int32_t type,
                                                        nmo_class_id_t class_id,
                                                        nmo_object_id_t sender);
/** Count `object` as waiting for messages, or stop (CKBeObject::SetAsWaitingForMessages). */
NMO_API void nmo_engine_block_set_waiting_for_messages(nmo_engine_block_t *block,
                                                       nmo_object_id_t object,
                                                       bool waiting);
/** Messages `object` received in the last delivery. */
NMO_API int nmo_engine_block_last_frame_message_count(const nmo_engine_block_t *block,
                                                      nmo_object_id_t object);
/** Type of the `pos`th of them; -1 when there is none. */
NMO_API int32_t nmo_engine_block_last_frame_message_type(const nmo_engine_block_t *block,
                                                         nmo_object_id_t object,
                                                         int pos);

#ifdef __cplusplus
}
#endif

#endif /* NMO_ENGINE_BLOCK_H */
