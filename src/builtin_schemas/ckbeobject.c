/**
 * @file ckbeobject.c
 * @brief CKBeObject deserialization implementation
 * 
 * Based on reference/src/CKBeObject.cpp::Load() (lines 474-580)
 */

#include "format/nmo_object.h"
#include "format/nmo_object_data.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk_parser.h"
#include "format/nmo_chunk_helpers.h"
#include "core/nmo_guid.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

// State save identifiers from reference/include/CKDefines2.h
#define CK_STATESAVE_BEHAVIORS      0x00000100
#define CK_STATESAVE_SCRIPTS        0x00000800
#define CK_STATESAVE_DATAS          0x00000040
#define CK_STATESAVE_NEWATTRIBUTES  0x00000011
#define CK_STATESAVE_ATTRIBUTES     0x00000010
#define CK_STATESAVE_SINGLEACTIVITY 0x00000400

// GUID for Attribute Manager from reference/include/CKEnums.h
// ATTRIBUTE_MANAGER_GUID = CKGUID(0x3d242466, 0)
#define ATTRIBUTE_MANAGER_GUID_D1 0x3d242466
#define ATTRIBUTE_MANAGER_GUID_D2 0x00000000

/**
 * @brief Deserialize CKBeObject from chunk
 * 
 * Based on CKBeObject::Load() from reference/src/CKBeObject.cpp (lines 474-580)
 * 
 * CKBeObject extends CKObject with:
 * - Scripts (behaviors) array
 * - Attributes with parameters
 * - Priority and waiting status
 * - Single activity flag
 * 
 * @param obj Object to deserialize into
 * @param parser Chunk parser (actually nmo_chunk_t for read operations)
 */
void ckbeobject_deserialize(nmo_object_t *obj, nmo_chunk_parser_t *parser) {
    nmo_chunk_t *chunk = (nmo_chunk_t *)parser;
    nmo_result_t result;
    
    // Create extended data structure for CKBeObject
    nmo_beobject_data_t *beobj_data = nmo_beobject_data_create(obj->arena);
    if (!beobj_data) {
        // Failed to allocate - abort deserialization
        return;
    }
    
    // Attach to object immediately
    nmo_object_set_beobject_data(obj, beobj_data);
    
    // Load base CKObject data first (visibility flags)
    // Note: In reference, this calls CKObject::Load()
    // The parser has already called parent deserializer before this
    
    nmo_chunk_start_read(chunk);
    
    // Check for legacy behaviors (version < 5)
    // Reference line 493: if (chunk->GetDataVersion() < 5 && chunk->SeekIdentifier(CK_STATESAVE_BEHAVIORS))
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORS);
    if (result.code == NMO_OK) {
        // Legacy behaviors - XObjectPointerArray
        // Reference lines 494-498: m_ScriptArray->Load(context, chunk);
        // Skip for now - requires XObjectPointerArray support
        (void)0; // TODO: Implement when needed
    }
    
    // Check for scripts (current version)
    // Reference line 500: if (chunk->SeekIdentifier(CK_STATESAVE_SCRIPTS))
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SCRIPTS);
    if (result.code == NMO_OK) {
        // Scripts array - XObjectPointerArray
        // Reference lines 501-505: m_ScriptArray->Load(context, chunk);
        
        // Use helper function to read object ID array
        // This encapsulates the XObjectPointerArray::Load() pattern:
        // 1. StartReadSequence() -> returns count
        // 2. For each: ReadObjectID()
        
        nmo_object_id_t *script_ids = NULL;
        size_t script_count = 0;
        result = nmo_chunk_read_object_id_array(chunk, &script_ids, &script_count, obj->arena);
        
        if (result.code == NMO_OK && script_count > 0) {
            // Store each script ID in CKBeObject data
            for (size_t i = 0; i < script_count; i++) {
                nmo_beobject_data_add_script(beobj_data, script_ids[i], obj->arena);
            }
        }
    }
    
    // Check for data flags (priority and waiting status)
    // Reference line 507: if (chunk->SeekIdentifier(CK_STATESAVE_DATAS))
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_DATAS);
    if (result.code == NMO_OK) {
        // Reference lines 508-521: Read version flag
        uint32_t version_flag = 0;
        result = nmo_chunk_read_dword(chunk, &version_flag);
        if (result.code == NMO_OK) {
            // Reference: if (versionFlag & 0x10000000)
            if (version_flag & 0x10000000) {
                // Read and store priority
                int32_t priority = 0;
                result = nmo_chunk_read_int(chunk, &priority);
                if (result.code == NMO_OK) {
                    beobj_data->priority = priority;
                }
            }
            
            // Check for waiting flag
            // Reference line 517: m_Waiting = ((versionFlag & 1) != 0);
            beobj_data->waiting_for_message = ((version_flag & 1) != 0);
        }
    }
    
    // Check for new attributes format
    // Reference line 529: if (chunk->SeekIdentifier(CK_STATESAVE_NEWATTRIBUTES))
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_NEWATTRIBUTES);
    if (result.code == NMO_OK) {
        // Reference lines 530-587: Complex attribute loading
        
        // Read attribute count
        size_t attr_count = 0;
        result = nmo_chunk_start_read_sequence(chunk, &attr_count);
        if (result.code == NMO_OK && attr_count > 0) {
            // Read attribute object IDs (parameters)
            // Reference lines 544-547
            nmo_object_id_t *attr_object_ids = (nmo_object_id_t *)malloc(sizeof(nmo_object_id_t) * attr_count);
            if (attr_object_ids) {
                for (size_t i = 0; i < attr_count; i++) {
                    result = nmo_chunk_read_object_id(chunk, &attr_object_ids[i]);
                    if (result.code != NMO_OK) {
                        attr_object_ids[i] = 0;
                    }
                }
                
                // Read attribute manager sequence
                // Reference line 559: chunk->StartManagerReadSequence(&managerGuid)
                nmo_guid_t manager_guid = NMO_GUID_NULL;
                size_t seq_count = 0;
                result = nmo_chunk_start_manager_read_sequence(chunk, &manager_guid, &seq_count);
                if (result.code == NMO_OK) {
                    // Check if it's the attribute manager GUID
                    // ATTRIBUTE_MANAGER_GUID = CKGUID(0x3d242466, 0)
                    bool is_attr_manager = (manager_guid.d1 == ATTRIBUTE_MANAGER_GUID_D1 && 
                                           manager_guid.d2 == ATTRIBUTE_MANAGER_GUID_D2);
                    
                    if (is_attr_manager && seq_count == attr_count) {
                        // Read each attribute
                        // Reference lines 561-585
                        for (size_t i = 0; i < seq_count; i++) {
                            // Read attribute type
                            size_t attr_type = 0;
                            result = nmo_chunk_start_read_sequence(chunk, &attr_type);
                            if (result.code == NMO_OK) {
                                nmo_object_id_t param_id = attr_object_ids[i];
                                // Store attribute in CKBeObject data
                                // Reference: SetAttribute(attrType, paramObj ? paramObj->GetID() : 0);
                                nmo_beobject_data_add_attribute(beobj_data, (int32_t)attr_type, param_id, obj->arena);
                            }
                        }
                    }
                }
                
                free(attr_object_ids);
            }
        }
    }
    
    // Check for legacy attributes format (for old files)
    // Reference line 589: if (chunk->SeekIdentifier(CK_STATESAVE_ATTRIBUTES))
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_ATTRIBUTES);
    if (result.code == NMO_OK) {
        // Legacy format - read count and skip
        int32_t legacy_count = 0;
        result = nmo_chunk_read_int(chunk, &legacy_count);
        if (result.code == NMO_OK && legacy_count > 0) {
            // TODO: Implement legacy attribute reading if needed
            (void)0;
        }
    }
    
    // Check for single activity flag
    // Reference line 654: if (chunk->SeekIdentifier(CK_STATESAVE_SINGLEACTIVITY))
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SINGLEACTIVITY);
    if (result.code == NMO_OK) {
        // Reference line 655: m_SingleActivity = TRUE;
        beobj_data->single_activity = true;
    }
}
