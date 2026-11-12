# Fuzz tests
macro(add_fuzz_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE nmo test_framework)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77)
endmacro()

add_fuzz_test(test_truncation_fuzz)

# Fuzz harnesses
#
# Each fuzz_<name>.c defines LLVMFuzzerTestOneInput. Every build links it with
# fuzz_replay.c into fuzz_<name>, which runs it once over the files it is given;
# CTest replays the seeds that fuzz_seeds writes, the inputs kept under
# regressions/<name>/ and, when present, the local sample corpus.
#
# With -DNMO_BUILD_FUZZERS=ON (Clang with libFuzzer, see the top-level
# CMakeLists.txt) each harness is also linked with libFuzzer as
# fuzz_<name>_libfuzzer. A run on macOS:
#
#   cmake -S . -B build-fuzz -G Ninja -DCMAKE_BUILD_TYPE=Debug \
#         -DCMAKE_C_COMPILER=$(brew --prefix llvm)/bin/clang -DNMO_BUILD_FUZZERS=ON
#   cmake --build build-fuzz --target fuzz_seeds fuzz_load_libfuzzer
#   build-fuzz/tests/fuzz/fuzz_seeds seeds data/Ballance/*.nmo
#   mkdir -p corpus/load
#   export LSAN_OPTIONS=suppressions=$PWD/tests/fuzz/lsan.supp
#   build-fuzz/tests/fuzz/fuzz_load_libfuzzer -malloc_limit_mb=1024 \
#         -jobs=100 -workers=4 corpus/load seeds/load
#
# fuzz_roundtrip takes the same seeds as fuzz_load; fuzz_chunk takes seeds/chunk
# and fuzz_obj seeds/obj. Big sample files make fuzz_load slow; -max_len=102400
# with the smaller seeds runs it about a hundred times faster. -jobs starts a new
# job after a crash; -fork stops at the first one. A crash leaves crash-<sha1>
# in the working directory: reproduce it with the replay binary (NMO_FUZZ_LOG=1
# logs the load), fix it, and keep an input under regressions/<name>/. Inputs
# mutated from sample files carry their content: commit one rebuilt from a
# generated seed instead.

set(NMO_FUZZ_HARNESSES load roundtrip chunk obj)
set(NMO_FUZZ_SEED_DIR "${CMAKE_CURRENT_BINARY_DIR}/seeds")

add_library(nmo_fuzz_common STATIC fuzz_common.c)
target_link_libraries(nmo_fuzz_common PUBLIC nmo)
target_include_directories(nmo_fuzz_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(nmo_fuzz_common PUBLIC
    NMO_FUZZ_DATA_DIR="${PROJECT_SOURCE_DIR}/data")

add_executable(fuzz_seeds fuzz_seeds.c)
target_link_libraries(fuzz_seeds PRIVATE nmo_fuzz_common nmo_project)

add_test(NAME fuzz_seeds COMMAND fuzz_seeds "${NMO_FUZZ_SEED_DIR}")
set_tests_properties(fuzz_seeds PROPERTIES FIXTURES_SETUP nmo_fuzz_seeds)

foreach(harness IN LISTS NMO_FUZZ_HARNESSES)
    add_executable(fuzz_${harness} fuzz_${harness}.c fuzz_replay.c)
    target_link_libraries(fuzz_${harness} PRIVATE nmo_fuzz_common)

    if(harness STREQUAL "roundtrip")
        set(_seed_subdir load)
    else()
        set(_seed_subdir ${harness})
    endif()
    add_test(NAME fuzz_${harness}_replay
             COMMAND fuzz_${harness}
                     "${NMO_FUZZ_SEED_DIR}/${_seed_subdir}"
                     "${CMAKE_CURRENT_SOURCE_DIR}/regressions/${harness}")
    set_tests_properties(fuzz_${harness}_replay PROPERTIES
        FIXTURES_REQUIRED nmo_fuzz_seeds
        SKIP_RETURN_CODE 77)

    if(NMO_BUILD_FUZZERS)
        add_executable(fuzz_${harness}_libfuzzer fuzz_${harness}.c)
        target_link_libraries(fuzz_${harness}_libfuzzer PRIVATE nmo_fuzz_common)
        target_link_options(fuzz_${harness}_libfuzzer PRIVATE -fsanitize=fuzzer)
    endif()
endforeach()

# The sample corpus is not in the repository; the test skips without it.
add_test(NAME fuzz_load_corpus_replay
         COMMAND fuzz_load "${PROJECT_SOURCE_DIR}/data/Ballance")
set_tests_properties(fuzz_load_corpus_replay PROPERTIES SKIP_RETURN_CODE 77)
