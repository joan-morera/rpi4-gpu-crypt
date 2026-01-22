cmake_minimum_required(VERSION 3.10)
project(aes256_gpu_provider)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Vulkan REQUIRED)
find_package(OpenSSL REQUIRED)

include_directories(include)
include_directories(src)
include_directories(${OPENSSL_INCLUDE_DIR})

file(GLOB_RECURSE LIB_SOURCES 
    "src/backend/*.cpp"
    "src/scheduler/*.cpp"
    "src/lib/*.cpp"
)

# 1. Build the shared library (internal C++ logic)
add_library(aes256_gpu SHARED ${LIB_SOURCES})
target_link_libraries(aes256_gpu Vulkan::Vulkan)

# 2. Build the OpenSSL Provider Module (loadable .so)
add_library(aes256_gpu_provider MODULE src/provider/aes256_provider.c)
target_link_libraries(aes256_gpu_provider aes256_gpu OpenSSL::Crypto)

# Compile shader rule
find_program(GLSLC_EXECUTABLE glslc)
if(GLSLC_EXECUTABLE)
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR})
    
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/aes256_ctr.spv
        COMMAND ${GLSLC_EXECUTABLE} ${CMAKE_SOURCE_DIR}/src/shaders/aes256_ctr.comp -o ${CMAKE_BINARY_DIR}/aes256_ctr.spv
        DEPENDS ${CMAKE_SOURCE_DIR}/src/shaders/aes256_ctr.comp
        COMMENT "Compiling AES-256 Shader"
    )
    add_custom_target(shaders ALL DEPENDS ${CMAKE_BINARY_DIR}/aes256_ctr.spv)
    
    install(FILES ${CMAKE_BINARY_DIR}/aes256_ctr.spv DESTINATION /usr/local/lib)
endif()

# Installation
install(TARGETS aes256_gpu DESTINATION /usr/local/lib)
install(TARGETS aes256_gpu_provider DESTINATION /usr/local/lib/ossl-modules)
install(FILES include/aes256_gpu.h DESTINATION /usr/local/include)
