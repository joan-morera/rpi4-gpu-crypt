cmake_minimum_required(VERSION 3.10)
project(rpi4-gpu-crypt)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

# Find OpenSSL
find_package(OpenSSL REQUIRED)

# Find Vulkan
find_package(Vulkan REQUIRED)

include_directories(${OPENSSL_INCLUDE_DIR} ${Vulkan_INCLUDE_DIRS})
include_directories(src)

# Shared Library for the Provider
add_library(vc6_crypto SHARED
    src/provider/entrypoint.c
    src/provider/ciphers.c
    src/backend/vulkan_ctx.cpp
    src/backend/memory.cpp
    src/scheduler/batcher.cpp
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
