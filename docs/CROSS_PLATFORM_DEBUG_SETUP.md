# Cross-Platform Iterator Debug Configuration

## Windows UCRT Debug Settings (Already Active)
Your Windows MSYS2 GCC 15 environment automatically enables strict iterator debugging:
```cpp
// Already enabled in Windows UCRT builds
#define _ITERATOR_DEBUG_LEVEL 2  // Maximum iterator validation
#define _HAS_ITERATOR_DEBUGGING 1
```

## Linux GCC Debug Settings
Add these flags to your CMake or build system for Linux debug builds:

```cmake
# In CMakeLists.txt for Debug builds on Linux
if(CMAKE_BUILD_TYPE MATCHES Debug AND UNIX AND NOT APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE 
        _GLIBCXX_DEBUG=1              # Enable libstdc++ debug mode
        _GLIBCXX_DEBUG_PEDANTIC=1     # Extra strict checking
        _GLIBCXX_ASSERTIONS=1         # Runtime assertions
    )
    target_compile_options(${PROJECT_NAME} PRIVATE
        -fsanitize=address            # AddressSanitizer
        -fsanitize=undefined          # UBSan for undefined behavior
        -fstack-protector-strong      # Stack protection
        -D_FORTIFY_SOURCE=2          # Buffer overflow protection
    )
    target_link_options(${PROJECT_NAME} PRIVATE
        -fsanitize=address
        -fsanitize=undefined
    )
endif()
```

## macOS Clang Debug Settings
```cmake
# In CMakeLists.txt for Debug builds on macOS
if(CMAKE_BUILD_TYPE MATCHES Debug AND APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE 
        _LIBCPP_DEBUG=1               # Enable libc++ debug mode
        _LIBCPP_DEBUG_LEVEL=1         # Iterator debugging
    )
    target_compile_options(${PROJECT_NAME} PRIVATE
        -fsanitize=address            # AddressSanitizer
        -fsanitize=undefined          # UBSan
        -fstack-protector-strong      # Stack protection
    )
    target_link_options(${PROJECT_NAME} PRIVATE
        -fsanitize=address
        -fsanitize=undefined
    )
endif()
```

## Runtime Debug Environment Variables

### Linux (GCC libstdc++)
```bash
# Enable debug mode at runtime
export GLIBCXX_FORCE_NEW=1
export MALLOC_CHECK_=2

# Run with debugging
./your_program
```

### macOS (Clang libc++)
```bash
# Enable debug mode at runtime  
export MallocStackLogging=1
export MallocScribble=1

# Run with debugging
./your_program
```

### All Platforms - Valgrind (Linux/macOS)
```bash
# Comprehensive memory checking
valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all \
         --track-origins=yes --verbose ./your_program

# For iterator debugging specifically
valgrind --tool=memcheck --track-origins=yes \
         --expensive-definedness-checks=yes ./your_program
```

## SOA-Specific Debugging

### Custom Debug Macros
Add these to your ParticleManager for development:

```cpp
#ifdef _DEBUG
#define SOA_BOUNDS_CHECK(container, index) \
    assert(index < container.size() && "SOA bounds violation")
    
#define SOA_CONSISTENCY_CHECK(soa) \
    assert(soa.isFullyConsistent() && "SOA vectors out of sync")
#else
#define SOA_BOUNDS_CHECK(container, index)
#define SOA_CONSISTENCY_CHECK(soa)
#endif
```

### Integration in Code
```cpp
// Use in your particle access code
for (size_t i = 0; i < particles.getSafeAccessCount(); ++i) {
    SOA_BOUNDS_CHECK(particles.positions, i);
    SOA_CONSISTENCY_CHECK(particles);
    
    // Safe access guaranteed
    auto& pos = particles.positions[i];
}
```

## Build Integration

Add this to your main CMakeLists.txt:

```cmake
# Cross-platform debug settings
if(CMAKE_BUILD_TYPE MATCHES Debug)
    message(STATUS "Enabling cross-platform iterator debugging")
    
    if(MSVC OR (WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "GNU"))
        # Windows - UCRT debug mode (automatic)
        target_compile_definitions(${PROJECT_NAME} PRIVATE 
            _ITERATOR_DEBUG_LEVEL=2
        )
    elseif(UNIX AND NOT APPLE)
        # Linux - libstdc++ debug mode
        target_compile_definitions(${PROJECT_NAME} PRIVATE 
            _GLIBCXX_DEBUG=1
            _GLIBCXX_DEBUG_PEDANTIC=1
        )
    elseif(APPLE)
        # macOS - libc++ debug mode
        target_compile_definitions(${PROJECT_NAME} PRIVATE 
            _LIBCPP_DEBUG=1
        )
    endif()
    
    # Universal sanitizers for all platforms
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${PROJECT_NAME} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        target_link_options(${PROJECT_NAME} PRIVATE
            -fsanitize=address,undefined
        )
    endif()
endif()
```

## Testing Your SOA Fixes

### Verification Commands
```bash
# Linux with debug mode
export GLIBCXX_DEBUG=1
./bin/debug/SDL3_Template

# Run particle manager tests specifically
./tests/test_scripts/run_particle_manager_tests.sh --verbose

# With Valgrind for comprehensive checking
valgrind --tool=memcheck ./bin/debug/SDL3_Template
```

This setup will make Linux and macOS debug builds as strict as Windows UCRT, ensuring you catch SOA synchronization issues across all platforms during development.