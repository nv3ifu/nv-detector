cmake_minimum_required(VERSION 3.20)

# Project metadata
project(NvDetector 
    VERSION 1.0.0
    DESCRIPTION "NvDetector Project"
    LANGUAGES CXX
)

# --- 1. Modern CMake Configuration ---
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Define output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# --- 2. Compiler Options & Warnings (Target-based) ---
# Create an interface library for compiler settings to avoid global pollution
add_library(project_options INTERFACE)

# C++20 Standard
target_compile_features(project_options INTERFACE cxx_std_20)

# Strict Warnings (Google Style)
# -Wall -Wextra: Standard warnings
# -Wpedantic: Warn on non-standard C++
# -Wshadow: Warn if variable declarations shadow others
# -Wconversion: Warn on type conversions that may lose data
# -Werror: Treat warnings as errors (Best practice for quality)
target_compile_options(project_options INTERFACE
    $<$<CXX_COMPILER_ID:Clang>:
        -Wall -Wextra -Wpedantic 
        -Wshadow -Wconversion -Wsign-conversion
        -Werror
    >
    $<$<CXX_COMPILER_ID:GNU>:
        -Wall -Wextra -Wpedantic 
        -Wshadow -Wconversion -Wsign-conversion
        -Werror
    >
    $<$<CXX_COMPILER_ID:MSVC>:
        /W4 /WX
    >
)

# --- 3. Static Analysis (Clang-Tidy) ---
find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
if(CLANG_TIDY_EXE)
    message(STATUS "Found clang-tidy: ${CLANG_TIDY_EXE}")
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
else()
    message(WARNING "clang-tidy not found! Static analysis disabled.")
endif()

# --- 4. Main Library Target ---
# Use GLOB_RECURSE with CONFIGURE_DEPENDS for automatic re-configuration when files change
file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS "src/*.cpp")

if(SRC_FILES)
    add_library(nv_detector SHARED ${SRC_FILES})
    
    # Add alias for cleaner usage
    add_library(NvDetector::Lib ALIAS nv_detector)

    # Apply options
    target_link_libraries(nv_detector 
        PUBLIC 
            project_options
        PRIVATE 
            dl 
            pthread
    )

    # Include directories
    target_include_directories(nv_detector PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )

    message(STATUS "Building nv_detector library with ${SRC_FILES}")
else()
    message(STATUS "No source files found in src/. 'nv_detector' target not created.")
endif()

# --- 5. Test Dynamic Library ---
# Build libdynamic_example.so for testing
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/dynamic_example.cpp")
    add_library(dynamic_example SHARED examples/dynamic_example.cpp)
    
    # Apply standard options
    target_link_libraries(dynamic_example PRIVATE project_options)
    
    # Set output directory to examples folder in build
    set_target_properties(dynamic_example PROPERTIES 
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples
        OUTPUT_NAME "dynamic_example"
        PREFIX "lib"
    )
    
    message(STATUS "Building test library: libdynamic_example.so")
endif()

# --- 6. Examples ---
file(GLOB_RECURSE EXAMPLE_FILES CONFIGURE_DEPENDS "examples/*_test.cpp")
set(EXAMPLES_OUTPUT_DIR ${CMAKE_BINARY_DIR}/examples)

foreach(EXAMPLE_SRC ${EXAMPLE_FILES})
    get_filename_component(EXAMPLE_NAME ${EXAMPLE_SRC} NAME_WE)
    
    add_executable(${EXAMPLE_NAME} ${EXAMPLE_SRC})
    
    # memory_leak_test 使用宽松的编译选项（故意包含内存泄漏示例）
    if(EXAMPLE_NAME STREQUAL "memory_leak_test")
        # 只使用 C++20 标准，不应用严格的警告选项
        target_compile_features(${EXAMPLE_NAME} PRIVATE cxx_std_20)
        # 禁用 clang-tidy 检查
        set_target_properties(${EXAMPLE_NAME} PROPERTIES CXX_CLANG_TIDY "")
        # 禁用所有警告和错误，添加调试信息
        target_compile_options(${EXAMPLE_NAME} PRIVATE -Wno-error -w -g)
    else()
        # 其他示例应用标准选项
        target_link_libraries(${EXAMPLE_NAME} PRIVATE project_options)
    endif()
    
    # Link main library if available
    if(TARGET nv_detector)
        target_link_libraries(${EXAMPLE_NAME} PRIVATE nv_detector)
    endif()

    target_include_directories(${EXAMPLE_NAME} PRIVATE include)
    
    # Set output directory specific for examples
    set_target_properties(${EXAMPLE_NAME} PROPERTIES 
        RUNTIME_OUTPUT_DIRECTORY ${EXAMPLES_OUTPUT_DIR}
    )
    
    # Make test executable depend on dynamic_example library
    if(TARGET dynamic_example)
        add_dependencies(${EXAMPLE_NAME} dynamic_example)
    endif()
endforeach()

# --- 7. Custom Target: plthook_test ---
# Run plthook_test with proper environment
if(TARGET plthook_test AND TARGET dynamic_example)
    add_custom_target(run_plthook_test
        COMMAND ${CMAKE_COMMAND} -E echo "Running PLT Hook test..."
        COMMAND ${EXAMPLES_OUTPUT_DIR}/plthook_test
        DEPENDS plthook_test dynamic_example
        WORKING_DIRECTORY ${EXAMPLES_OUTPUT_DIR}
        COMMENT "Running plthook_test with libdynamic_example.so"
    )
endif()

# --- 8. Custom Target: memory_leak_test ---
# Run memory_leak_test
if(TARGET memory_leak_test)
    add_custom_target(run_memory_leak_test
        COMMAND ${EXAMPLES_OUTPUT_DIR}/memory_leak_test
        DEPENDS memory_leak_test
        WORKING_DIRECTORY ${EXAMPLES_OUTPUT_DIR}
        COMMENT "Running memory leak detection test"
    )
endif()
