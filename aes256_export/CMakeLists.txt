cmake_minimum_required(VERSION 3.10)
project(aes256_gpu_provider)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Vulkan REQUIRED)

include_directories(include)
include_directories(src)

file(GLOB_RECURSE SOURCES 
    "src/backend/*.cpp"
    "src/scheduler/*.cpp"
    "src/lib/*.cpp"
)

# Build as a shared library
add_library(aes256_gpu SHARED ${SOURCES})
target_link_libraries(aes256_gpu Vulkan::Vulkan)

# Compile shader rule (Optional, if user wants to build shaders)
find_program(GLSLC_EXECUTABLE glslc)
if(GLSLC_EXECUTABLE)
    # Ensure output directory exists
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR})
    
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/aes256_ctr.spv
        COMMAND ${GLSLC_EXECUTABLE} ${CMAKE_SOURCE_DIR}/src/shaders/aes256_ctr.comp -o ${CMAKE_BINARY_DIR}/aes256_ctr.spv
        DEPENDS ${CMAKE_SOURCE_DIR}/src/shaders/aes256_ctr.comp
        COMMENT "Compiling AES-256 Shader"
    )
    add_custom_target(shaders ALL DEPENDS ${CMAKE_BINARY_DIR}/aes256_ctr.spv)
    
    # Installation instruction: Copy shader to /usr/local/lib/
    install(FILES ${CMAKE_BINARY_DIR}/aes256_ctr.spv DESTINATION /usr/local/lib)
endif()

install(TARGETS aes256_gpu DESTINATION /usr/local/lib)
install(FILES include/aes256_gpu.h DESTINATION /usr/local/include)
