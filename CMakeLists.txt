cmake_minimum_required(VERSION 3.10)
project(rpi4-gpu-crypt)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

# Find OpenSSL
find_package(OpenSSL REQUIRED)

# Find Vulkan with glslc
find_package(Vulkan REQUIRED)
find_program(GLSLC glslc HINTS ${Vulkan_GLSLC_EXECUTABLE})

# Compile Shader
set(SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/aes_ctr.comp")
set(SHADER_BINARY "${CMAKE_CURRENT_BINARY_DIR}/aes_ctr.spv")

add_custom_command(
    OUTPUT ${SHADER_BINARY}
    COMMAND ${GLSLC} -O ${SHADER_SOURCE} -o ${SHADER_BINARY}
    DEPENDS ${SHADER_SOURCE}
    COMMENT "Compiling GLSL shader"
)

# Shared Library for the Provider
add_library(vc6_crypto SHARED
    src/provider/entrypoint.c
    src/provider/ciphers.c
    src/backend/vulkan_ctx.cpp
    src/backend/memory.cpp
    src/scheduler/batcher.cpp
    ${SHADER_BINARY}
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
