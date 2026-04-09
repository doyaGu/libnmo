# Performance Benchmarks

Micro-benchmarks that guard against performance regressions in core
data structures, the type system, file I/O paths, and object queries.

## Benchmark files

### test_performance.c

General data-structure smoke tests (no test framework, standalone `main`):

- **Hash table reserve** -- 10 000 inserts with and without `nmo_hash_table_reserve`, reports speedup.
- **Arena reserve** -- 10 000 x 64-byte allocations with and without `nmo_arena_reserve`.
- **Object index lookup** -- 10 000 class-id lookups on 1 000 objects, linear scan vs. `nmo_object_index`.

### test_type_system_bench.c

Type-registry throughput (currently disabled -- needs API update):

- Registration throughput (500 types, target < 0.5 ms each)
- GUID lookup latency (10 000 lookups, target < 0.05 ms each)
- Name lookup latency (10 000 lookups, target < 0.1 ms each)
- Enum and flags registration (100 each, target < 1.0 ms each)
- Memory usage per type descriptor (target < 2 KB)
- Unregistration throughput (500 types)

### test_load_save_mmap_baseline.c

End-to-end file I/O benchmarks on real NMO/CMO samples:

- **Load** -- repeated `nmo_load_file` (default 3 iterations)
- **Save** -- repeated `nmo_save_file` with schema validation (default 2 iterations)
- **Mmap scan** -- full sequential read of memory-mapped file (default 5 iterations)

This is the only benchmark with CI threshold enforcement (see below).

### test_index_queries.c

Object-repository query performance on a synthetic 20 000-object repository:

- **Class-id lookup** -- 2 000 iterations, linear scan vs. indexed lookup, reports speedup.
- **GUID lookup** -- 2 000 iterations, linear scan vs. indexed lookup, reports speedup.

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

All benchmarks in this directory are built automatically. They are
registered with CTest under the `performance` label.

## Running

Run all performance tests:

```bash
ctest --test-dir build -L performance --output-on-failure
```

Run a single benchmark:

```bash
./build/tests/performance/test_performance
```

## CI enforcement

The `test_load_save_mmap_baseline` benchmark supports threshold-gated CI
via environment variables. When `NMO_BENCH_ENFORCE=1` is set, each
sample must finish within the configured limits or the test fails.

| Variable                | Default  | CI value | Description                 |
|-------------------------|----------|----------|-----------------------------|
| `NMO_BENCH_ENFORCE`    | `0`      | `1`      | Enable threshold assertions |
| `NMO_BENCH_LOAD_ITERS` | `3`      | `3`      | Load iterations per sample  |
| `NMO_BENCH_SAVE_ITERS` | `2`      | `2`      | Save iterations per sample  |
| `NMO_BENCH_MMAP_ITERS` | `5`      | `5`      | Mmap iterations per sample  |
| `NMO_BENCH_MAX_LOAD_MS`| `3000`   | `5000`   | Max load time (ms)          |
| `NMO_BENCH_MAX_SAVE_MS`| `4000`   | `5000`   | Max save time (ms)          |
| `NMO_BENCH_MAX_MMAP_MS`| `1000`   | `1500`   | Max mmap scan time (ms)     |

Without `NMO_BENCH_ENFORCE`, the benchmark prints timings but always
passes, making local runs informational only.

## Interpreting results

Each benchmark prints per-operation timings and, where applicable, a
speedup ratio (e.g. indexed vs. linear lookup). A speedup below 1.0x
means the optimization is slower than the baseline -- investigate.
When CI enforcement is active, any timing that exceeds the configured
threshold fails the test and blocks the pipeline.
