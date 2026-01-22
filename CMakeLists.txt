cmake_minimum_required(VERSION 3.10)
project(rpi4-gpu-crypt)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

# Find OpenSSL
find_package(OpenSSL REQUIRED)

# Find Vulkan with glslc OR glslangValidator
find_package(Vulkan REQUIRED)
find_program(GLSLC glslc HINTS ${Vulkan_GLSLC_EXECUTABLE})

if(NOT GLSLC)
    find_program(GLSLANG glslangValidator)
    if(GLSLANG)
        message(STATUS "Found glslangValidator: ${GLSLANG}")
        set(GLSLC_CMD ${GLSLANG} -V)
    else()
        message(FATAL_ERROR "No shader compiler found (glslc or glslangValidator)")
    endif()
else()
    set(GLSLC_CMD ${GLSLC} -O)
endif()

# Compile AES-128 Shader
set(SHADER_SOURCE_AES128 "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/aes128_ctr.comp")
set(SHADER_BINARY_AES128 "${CMAKE_CURRENT_BINARY_DIR}/aes128_ctr.spv")

add_custom_command(
    OUTPUT ${SHADER_BINARY_AES128}
    COMMAND ${GLSLC_CMD} ${SHADER_SOURCE_AES128} -o ${SHADER_BINARY_AES128}
    DEPENDS ${SHADER_SOURCE_AES128}
    COMMENT "Compiling AES-128 GLSL shader"
)

# Compile AES-256 Shader
set(SHADER_SOURCE_AES256 "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/aes256_ctr.comp")
set(SHADER_BINARY_AES256 "${CMAKE_CURRENT_BINARY_DIR}/aes256_ctr.spv")

add_custom_command(
    OUTPUT ${SHADER_BINARY_AES256}
    COMMAND ${GLSLC_CMD} ${SHADER_SOURCE_AES256} -o ${SHADER_BINARY_AES256}
    DEPENDS ${SHADER_SOURCE_AES256}
    COMMENT "Compiling AES-256 GLSL shader"
)

set(SHADER_SOURCE_CHACHA "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/chacha20.comp")
set(SHADER_BINARY_CHACHA "${CMAKE_CURRENT_BINARY_DIR}/chacha20.spv")

add_custom_command(
    OUTPUT ${SHADER_BINARY_CHACHA}
    COMMAND ${GLSLC_CMD} ${SHADER_SOURCE_CHACHA} -o ${SHADER_BINARY_CHACHA}
    DEPENDS ${SHADER_SOURCE_CHACHA}
    COMMENT "Compiling ChaCha20 GLSL shader"
)

set(SHADER_SOURCE_RC4 "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/rc4.comp")
set(SHADER_BINARY_RC4 "${CMAKE_CURRENT_BINARY_DIR}/rc4.spv")

add_custom_command(
    OUTPUT ${SHADER_BINARY_RC4}
    COMMAND ${GLSLC_CMD} ${SHADER_SOURCE_RC4} -o ${SHADER_BINARY_RC4}
    DEPENDS ${SHADER_SOURCE_RC4}
    COMMENT "Compiling RC4 GLSL shader"
)

# Shared Library for the Provider
add_library(vc6_crypto SHARED
    src/provider/entrypoint.c
    src/provider/ciphers.c
    src/backend/vulkan_ctx.cpp
    src/backend/memory.cpp
    src/scheduler/batcher.cpp
    ${SHADER_BINARY_AES128}
    ${SHADER_BINARY_AES256}
    ${SHADER_BINARY_CHACHA}
    ${SHADER_BINARY_RC4}
)

target_link_libraries(vc6_crypto
    OpenSSL::Crypto
    Vulkan::Vulkan
)

# Compiler flags
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(vc6_crypto PRIVATE -Wall -Wextra -fPIC)
endif()

# Test/Benchmark Executable
add_executable(bench_runner tests/bench_runner.cpp)
target_link_libraries(bench_runner vc6_crypto OpenSSL::Crypto Vulkan::Vulkan)
