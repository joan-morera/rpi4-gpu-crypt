#ifndef AES256_GPU_H
#define AES256_GPU_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle
typedef struct AES256Context AES256Context;

// Initialize the GPU context and AES-256 batcher
// Returns NULL on failure
void *aes256_gpu_init();

// Cleanup resources
void aes256_gpu_cleanup(void *handle);

// Submit encryption job
// Returns 1 on success, 0 on failure
// key must be 32 bytes, iv must be 16 bytes.
int aes256_gpu_encrypt(void *handle, const unsigned char *in,
                       unsigned char *out, size_t len, const unsigned char *key,
                       const unsigned char *iv);

#ifdef __cplusplus
}
#endif

#endif // AES256_GPU_H
