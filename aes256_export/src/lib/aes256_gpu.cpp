#include "../../include/aes256_gpu.h"
#include "../backend/vulkan_ctx.hpp"
#include "../scheduler/aes256_batcher.hpp"
#include <cstdio>
#include <exception>

struct AES256Context {
  VulkanContext *vk_ctx;
  AES256Batcher *batcher;
};

extern "C" {
void *aes256_gpu_init() {
  try {
    auto *ctx = new AES256Context();
    ctx->vk_ctx = new VulkanContext();
    ctx->batcher = new AES256Batcher(ctx->vk_ctx);
    return (void *)ctx;
  } catch (const std::exception &e) {
    fprintf(stderr, "[AES256_GPU] Init failed: %s\n", e.what());
    return nullptr;
  } catch (...) {
    fprintf(stderr, "[AES256_GPU] Init failed: Unknown error\n");
    return nullptr;
  }
}

void aes256_gpu_cleanup(void *handle) {
  if (!handle)
    return;
  auto *ctx = (AES256Context *)handle;
  if (ctx->batcher)
    delete ctx->batcher;
  if (ctx->vk_ctx)
    delete ctx->vk_ctx;
  delete ctx;
}

int aes256_gpu_encrypt(void *handle, const unsigned char *in,
                       unsigned char *out, size_t len, const unsigned char *key,
                       const unsigned char *iv) {
  if (!handle)
    return 0;
  auto *ctx = (AES256Context *)handle;
  return ctx->batcher->submit(in, out, len, key, iv) ? 1 : 0;
}
}
