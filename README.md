# libnmo

**libnmo** is a production-ready C library for reading and writing Virtools file formats (`.nmo`, `.cmo`, `.vmo`) with full compatibility with the original Virtools runtime.

## Features

- **Complete Format Support**: Load and save Virtools files (versions 2-9)
- **Layered Architecture**: Clean separation of concerns with zero circular dependencies
- **Schema-Driven**: Pre-generated schemas for type safety
- **Production-Ready**: Bounds-checked, comprehensive error handling, extensive tests
- **Extensible**: Custom manager and schema support
- **Cross-Platform**: Windows, Linux, macOS support
- **CLI Tools**: Inspect, validate, convert, and diff Virtools files

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    APPLICATION LAYER                        │
│  - User applications, CLI tools                            │
└─────────────────────────┬───────────────────────────────────┘
                          │
┌─────────────────────────▼───────────────────────────────────┐
│                    SESSION LAYER                            │
│  - Context, Session, Parser, Builder                        │
└─────────────────────────┬───────────────────────────────────┘
                          │
┌─────────────────────────▼───────────────────────────────────┐
│                      OBJECT LAYER                           │
│  - Object repository, ID remapping                          │
└─────────────────────────┬───────────────────────────────────┘
                          │
┌─────────────────────────▼───────────────────────────────────┐
│                      SCHEMA LAYER                           │
│  - Schema registry, validator, migrator                     │
└─────────────────────────┬───────────────────────────────────┘
                          │
┌─────────────────────────▼───────────────────────────────────┐
│                      FORMAT LAYER                           │
│  - Header, chunk, object, manager serialization             │
└─────────────────────────┬───────────────────────────────────┘
                          │
┌─────────────────────────▼───────────────────────────────────┐
│                        IO LAYER                             │
│  - File, memory, compressed, checksummed, transactional IO  │
└─────────────────────────┬───────────────────────────────────┘
                          │
┌─────────────────────────▼───────────────────────────────────┐
│                       CORE LAYER                            │
│  - Allocator, arena, error, logger, GUID                    │
└─────────────────────────────────────────────────────────────┘
```

## Building

### Prerequisites

- CMake 3.15 or later
- C17-compatible compiler (GCC, Clang, MSVC)
- zlib development library

### Linux/macOS

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

### Windows

```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

### Build Options

- `NMO_BUILD_TESTS` - Build test suite (default: ON)
- `NMO_BUILD_TOOLS` - Build CLI tools (default: ON)
- `NMO_BUILD_EXAMPLES` - Build examples (default: ON)
- `NMO_BUILD_SHARED` - Build shared library (default: OFF)
- `NMO_ENABLE_SIMD` - Enable SIMD optimizations (default: OFF)

Example:
```bash
cmake -DNMO_BUILD_SHARED=ON -DCMAKE_BUILD_TYPE=Release ..
```

## Quick Start

```c
#include <nmo.h>

int main(int argc, char** argv) {
    // Create context
    nmo_context* ctx = nmo_context_create(&(nmo_context_desc){
        .allocator = NULL,  // Use default
        .logger = nmo_logger_stderr(),
        .thread_pool_size = 4
    });

    // Register built-in schemas
    nmo_schema_registry_add_builtin(nmo_context_get_schema_registry(ctx));

    // Create session
    nmo_session* session = nmo_session_create(ctx);

    // Load file
    nmo_result result = nmo_load_file(session, argv[1], NMO_LOAD_DEFAULT);
    if (result.code != NMO_OK) {
        fprintf(stderr, "Error: %s\n", result.error->message);
        return 1;
    }

    // Get file info
    nmo_file_info info = nmo_session_get_file_info(session);
    printf("Object Count: %u\n", info.object_count);

    // Clean up
    nmo_session_destroy(session);
    nmo_context_release(ctx);
    return 0;
}
```

## CLI Tools

### nmo-inspect

Inspect Virtools file contents:

```bash
nmo-inspect file.nmo                 # Text output
nmo-inspect --format=json file.nmo   # JSON output
nmo-inspect --verbose file.nmo       # Verbose output
```

### nmo-validate

Validate Virtools files:

```bash
nmo-validate file.nmo                # Validate file
nmo-validate --strict file.nmo       # Strict mode
```

### nmo-convert

Convert between Virtools file versions:

```bash
nmo-convert input.nmo output.nmo --version=8
nmo-convert input.nmo output.nmo --compress
```

### nmo-diff

Compare two Virtools files:

```bash
nmo-diff file1.nmo file2.nmo
```

## Documentation

- [Implementation Plan](IMPLEMENTATION_PLAN.md) - Comprehensive development plan
- [Development Checklist](DEVELOPMENT_CHECKLIST.md) - Phase-by-phase checklist
- [API Documentation](docs/libnmo.md) - API reference
- [File Format Specification](docs/VIRTOOLS_FILE_FORMAT_SPEC.md) - Format details
- [Implementation Guide](docs/VIRTOOLS_IMPLEMENTATION_GUIDE.md) - Implementation notes

## Testing

Run tests:

```bash
cd build
ctest
```

Run with verbose output:

```bash
ctest --verbose
```

Run specific test:

```bash
./tests/unit/test_allocator
```

## Contributing

Contributions are welcome! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Project Status

**Status**: ✅ Active Development

### Implementation Progress

- ✅ Phase 1: Project Setup
- 🚧 Phase 2: Core Layer (in progress)
- ⏳ Phase 3-14: Pending

See [DEVELOPMENT_CHECKLIST.md](DEVELOPMENT_CHECKLIST.md) for detailed progress tracking.

## Acknowledgments

This project implements the Virtools file format based on extensive reverse engineering and documentation efforts by the community.

## Support

For issues, questions, or contributions:
- GitHub Issues: [doyaGu/libnmo/issues](https://github.com/doyaGu/libnmo/issues)
- Documentation: See `docs/` directory
