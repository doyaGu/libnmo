# Contributing to libnmo

Thank you for your interest in contributing to libnmo! This document provides guidelines and instructions for contributing.

## Code of Conduct

- Be respectful and constructive
- Focus on technical merit
- Help others learn and grow
- Follow project conventions

## Getting Started

1. Fork the repository
2. Clone your fork: `git clone https://github.com/YOUR_USERNAME/libnmo.git`
3. Create a branch: `git checkout -b my-feature`
4. Make your changes
5. Test your changes
6. Commit and push
7. Open a pull request

## Development Setup

### Prerequisites

- CMake 3.15+
- C17-compatible compiler
- zlib development library
- Git

### Building

```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
cmake --build .
```

### Running Tests

```bash
cd build
ctest --verbose
```

## Coding Standards

### Style

- **Indentation**: 4 spaces (no tabs)
- **Line length**: 100 characters max
- **Braces**: K&R style (opening brace on same line)
- **Naming**:
  - Functions: `nmo_module_function_name`
  - Types: `nmo_type_name`
  - Enums: `NMO_ENUM_VALUE`
  - Macros: `NMO_MACRO_NAME`
- **Comments**: Use `/* */` for multi-line, `//` for single-line

### Example

```c
/**
 * @brief Allocate memory from arena
 * @param arena Arena allocator
 * @param size Size in bytes
 * @param alignment Alignment requirement
 * @return Pointer to allocated memory or NULL on failure
 */
void* nmo_arena_alloc(nmo_arena* arena, size_t size, size_t alignment) {
    if (!arena || size == 0) {
        return NULL;
    }

    // Align pointer
    uintptr_t aligned = (arena->cursor + alignment - 1) & ~(alignment - 1);

    // Check bounds
    if (aligned + size > arena->end) {
        return NULL;
    }

    void* ptr = (void*)aligned;
    arena->cursor = aligned + size;
    return ptr;
}
```

### Architecture Rules

1. **No circular dependencies**: Always depend downward in the layer hierarchy
2. **Explicit ownership**: Use arena allocation, reference counting, or clear ownership
3. **Error handling**: Always check return values and propagate errors
4. **Memory safety**: Bounds-check all array accesses
5. **Platform independence**: Use portable types and abstractions

### Layer Dependencies

```
Core → IO → Format → Schema → Session → App
  ↑      ↑      ↑       ↑        ↑       ↑
  └──────┴──────┴───────┴────────┴───────┘
       (no upward dependencies)
```

## Testing

### Unit Tests

- Test each function in isolation
- Use the test framework in `tests/test_framework.h`
- Aim for >80% code coverage
- Test edge cases and error conditions

Example:

```c
#include "test_framework.h"
#include "core/nmo_arena.h"

TEST(arena, basic_allocation) {
    nmo_arena* arena = nmo_arena_create(NULL, 1024);
    ASSERT_NE(NULL, arena);

    void* ptr = nmo_arena_alloc(arena, 100, 8);
    ASSERT_NE(NULL, ptr);

    nmo_arena_destroy(arena);
}
```

### Integration Tests

- Test complete workflows
- Test interactions between components
- Test with real or realistic data

### Fuzz Tests

- Test parsers with malformed input
- Test for crashes and memory corruption
- Use AFL or libFuzzer

## Pull Request Process

1. **Before submitting**:
   - Run all tests and ensure they pass
   - Run static analysis (cppcheck, clang-tidy)
   - Check for memory leaks (valgrind)
   - Update documentation if needed
   - Add tests for new functionality

2. **PR description should include**:
   - What changes were made and why
   - Which issue(s) this addresses (if any)
   - Testing performed
   - Screenshots (if UI changes)

3. **Review process**:
   - At least one maintainer approval required
   - All CI checks must pass
   - Address all review comments
   - Squash commits before merge (if requested)

## Documentation

- Add Doxygen comments to public APIs
- Update README.md if user-facing changes
- Add examples for new features
- Update CHANGELOG.md

### Doxygen Format

```c
/**
 * @brief Short description
 *
 * Longer description with more details.
 *
 * @param param1 Description of param1
 * @param param2 Description of param2
 * @return Description of return value
 * @retval NMO_OK Success
 * @retval NMO_ERR_NOMEM Out of memory
 */
```

## Commit Messages

Format:
```
<type>(<scope>): <subject>

<body>

<footer>
```

Types:
- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation only
- `style`: Code style changes (formatting)
- `refactor`: Code refactoring
- `test`: Adding tests
- `chore`: Build/tooling changes

Example:
```
feat(io): add memory-mapped file support

Implement memory-mapped IO for better performance with large files.
Uses mmap on POSIX and CreateFileMapping on Windows.

Closes #42
```

## Reporting Issues

### Bug Reports

Include:
- libnmo version
- Operating system and version
- Compiler and version
- Steps to reproduce
- Expected vs actual behavior
- Error messages or logs
- Minimal test case (if possible)

### Feature Requests

Include:
- Use case description
- Proposed API (if applicable)
- Alternative solutions considered
- Impact on existing code

## Questions?

- Open an issue for general questions
- Check existing documentation first
- Be specific and provide context

Thank you for contributing to libnmo!
