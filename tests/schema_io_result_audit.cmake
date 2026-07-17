file(GLOB schema_files "${SOURCE_DIR}/src/object/builtin/*_schemas.c")
set(failures "")

list(LENGTH schema_files schema_file_count)
if(NOT schema_file_count EQUAL 43)
    string(APPEND failures
        "expected 43 built-in schema implementations, found ${schema_file_count}\n")
endif()

foreach(schema_file IN LISTS schema_files)
    file(READ "${schema_file}" schema_contents)
    if(NOT schema_contents MATCHES "_deserialize[ 	\r\n]*\\(")
        string(APPEND failures
            "${schema_file}: missing deserialize implementation\n")
    endif()
    if(NOT schema_contents MATCHES "_serialize[ 	\r\n]*\\(")
        string(APPEND failures
            "${schema_file}: missing serialize implementation\n")
    endif()

    file(STRINGS "${schema_file}" lines)
    set(line_number 0)
    set(in_dependency_remap FALSE)
    foreach(line IN LISTS lines)
        math(EXPR line_number "${line_number} + 1")
        if(in_dependency_remap AND
           (line MATCHES "^nmo_status_t " OR line MATCHES "^static ") AND
           NOT line MATCHES "^nmo_status_t nmo_.*_remap_dependencies\\(")
            set(in_dependency_remap FALSE)
        endif()
        if(line MATCHES "^nmo_status_t nmo_.*_remap_dependencies\\(")
            set(in_dependency_remap TRUE)
        endif()

        if(in_dependency_remap AND line MATCHES "nmo_object_repository_find_by_id")
            string(APPEND failures
                "${schema_file}:${line_number}: dependency remap must not discard unresolved references: ${line}\n")
        endif()

        if(in_dependency_remap AND
           (line MATCHES "->[A-Za-z0-9_\.]+[ \t]*=[^=]" OR
            line MATCHES "nmo_array_(clear|remove|resize|swap)[ \t]*\\("))
            string(APPEND failures
                "${schema_file}:${line_number}: dependency remap must not normalize object state: ${line}\n")
        endif()

        if((line MATCHES "\\(void\\)[ \t]*nmo_chunk_(read|write|skip|seek_identifier)" OR
            line MATCHES "^[ \t]*nmo_chunk_(read|write|skip|seek_identifier)[A-Za-z0-9_]*[ \t]*\\(" OR
            line MATCHES "nmo_chunk_read_string[ \t]*\\(" OR
            line MATCHES "nmo_chunk_read_and_fill_buffer(_nosize)?[ \t]*\\(" OR
            line MATCHES "if[ \t]*\\(.*nmo_chunk_(read|write|skip)" OR
            line MATCHES "if[ \t]*\\(.*nmo_chunk_seek_identifier" OR
            line MATCHES "if[ \t]*\\(result[ \t]*==[ \t]*NMO_OK[ \t]*&&" OR
            line MATCHES "if[ \t]*\\(result[ \t]*!=[ \t]*NMO_OK\\)[ \t]*break") AND
           NOT line MATCHES "_checked")
            string(APPEND failures "${schema_file}:${line_number}: ${line}\n")
        endif()

        if(line MATCHES "goto[ \t]+load_")
            string(APPEND failures
                "${schema_file}:${line_number}: chunk I/O errors must not skip to a later section: ${line}\n")
        endif()

    endforeach()
endforeach()

foreach(schema_file IN LISTS schema_files)
    file(READ "${schema_file}" contents)
    if(contents MATCHES
       "if[ \t]*\\([^\\)]*count[ \t]*<[ \t]*0[^\\)]*\\)[ \t\\r\\n]*\\{?[^\\}]*count[ \t]*=[ \t]*0")
        string(APPEND failures
            "${schema_file}: silently normalizes a negative serialized count\n")
    endif()
    if(contents MATCHES
       "if[ \t]*\\(result[ \t]*!=[ \t]*NMO_OK\\)[ \t\\r\\n]*\\{[^\\}]*break[ \t]*;")
        string(APPEND failures
            "${schema_file}: suppresses a chunk I/O failure with break\n")
    endif()
    if(contents MATCHES
       "if[ \t\r\n]*\\([^\\)]*nmo_chunk_seek_identifier[ \t\r\n]*\\(")
        string(APPEND failures
            "${schema_file}: branches directly on a chunk seek result\n")
    endif()
    if(contents MATCHES
       "if[ \t\r\n]*\\([A-Za-z_][A-Za-z0-9_]*[ \t]*!=[ \t]*NMO_OK[ \t]*\\)[ \t\r\n]*\\{[ \t\r\n]*NMO_RETURN_ERROR\\([ \t]*NMO_ERR_")
        string(APPEND failures
            "${schema_file}: replaces a checked operation error with a fixed status\n")
    endif()
    if(contents MATCHES
       "if[ \t\r\n]*\\([A-Za-z_][A-Za-z0-9_]*[ \t]*!=[ \t]*NMO_OK[ \t]*\\)[ \t\r\n]*(return[ \t]+NMO_ERR_|NMO_RETURN_ERROR\\([ \t]*NMO_ERR_)")
        string(APPEND failures
            "${schema_file}: replaces a checked operation error with a fixed status\n")
    endif()
endforeach()

if(NOT failures STREQUAL "")
    message(FATAL_ERROR "Ignored chunk I/O result(s):\n${failures}")
endif()
