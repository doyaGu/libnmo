# Source encoding audit
#
# Fails when a C source or header under src/, include/, tools/, tests/, or
# examples/ starts with a UTF-8 byte order mark or contains byte sequences that
# are the signature of text decoded as GBK and re-encoded as UTF-8 (the
# "mojibake" that has repeatedly corrupted dashes, arrows, and box-drawing
# characters in this repository). Legitimate non-ASCII text is allowed.
#
# Usage: cmake -DSOURCE_DIR=<repo root> -P source_encoding_audit.cmake

if(NOT SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(GLOB_RECURSE source_files
    "${SOURCE_DIR}/src/*.c" "${SOURCE_DIR}/src/*.h"
    "${SOURCE_DIR}/include/*.h"
    "${SOURCE_DIR}/tools/*.c" "${SOURCE_DIR}/tools/*.h"
    "${SOURCE_DIR}/tests/*.c" "${SOURCE_DIR}/tests/*.h"
    "${SOURCE_DIR}/examples/*.c" "${SOURCE_DIR}/examples/*.h")

string(ASCII 239 187 191 utf8_bom)

# Characters that only appear when UTF-8 punctuation is misdecoded as GBK.
set(mojibake_markers "鈹" "闁" "陋" "隆" "芒鈧" "鈥" "脙" "鈫")

set(failures "")
foreach(source_file IN LISTS source_files)
    file(READ "${source_file}" contents)
    file(RELATIVE_PATH relative_path "${SOURCE_DIR}" "${source_file}")

    string(FIND "${contents}" "${utf8_bom}" bom_index)
    if(bom_index EQUAL 0)
        string(APPEND failures "${relative_path}: starts with a UTF-8 byte order mark\n")
    endif()

    foreach(marker IN LISTS mojibake_markers)
        string(FIND "${contents}" "${marker}" marker_index)
        if(NOT marker_index EQUAL -1)
            string(APPEND failures
                "${relative_path}: contains GBK-decoded text ('${marker}'); re-save the file as UTF-8\n")
            break()
        endif()
    endforeach()
endforeach()

if(failures)
    message(FATAL_ERROR "Source encoding audit failed:\n${failures}")
endif()

list(LENGTH source_files source_file_count)
message(STATUS "Source encoding audit passed for ${source_file_count} files")
