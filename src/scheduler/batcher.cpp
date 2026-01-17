#include "batcher.hpp"
#include <cstring>
#include <iostream>

#define RING_SIZE 1024 * 1024 * 16 // 16MB Ring Buffer

Batcher::Batcher(VulkanContext *ctx) : ctx(ctx), running(true) {
  // initialize ring buffers logic (placeholder)
  // createBuffer(ctx->getDevice(), ctx->getPhysicalDevice(), RING_SIZE,
  // VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
  // VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, inputRing.buffer, inputRing.memory);

  // Start background thread
  workerThread = std::thread(&Batcher::workerLoop, this);
}

Batcher::~Batcher() {
  running = false;
  queueCv.notify_all();
  if (workerThread.joinable())
    workerThread.join();
}

bool Batcher::submit(const unsigned char *in, unsigned char *out, size_t len,
                     const unsigned char *key, const unsigned char *iv) {
  // In a real implementation:
  // 1. Copy 'in' to mapped Ring Buffer (Zero Copy from Nginx/App to Vulkan
  // Staging)
  // 2. Add job to queue
  // 3. Notify worker
  // 4. Wait for completion (CV wait)

  // For MVP proof:
  std::unique_lock<std::mutex> lock(queueMutex);
  // ... add to internal tracking ...
  queueCv.notify_one();

  // Emulate blocking wait
  // cv.wait(lock, ...);

  // Fallback CPU copy for proof of compilation without full logic
  memcpy(out, in, len);

  return true;
}

void Batcher::workerLoop() {
  while (running) {
    std::unique_lock<std::mutex> lock(queueMutex);
    queueCv.wait_for(lock, std::chrono::milliseconds(5),
                     [this] { return !running /* || has pending jobs */; });

    if (!running)
      break;

    // dispatchBatch();
  }
}

void Batcher::dispatchBatch() {
  // Record Command Buffer
  // vkQueueSubmit
  // vkQueueWaitIdle (or better, Fence)
}

extern "C" {
void *vc6_init() {
  VulkanContext *ctx = new VulkanContext();
  Batcher *batcher = new Batcher(ctx);
  return (void *)batcher;
}

void vc6_cleanup(void *handle) {
  Batcher *batcher = (Batcher *)handle;
  delete batcher;
  // logic to delete ctx as well would be needed, or refcount it
}

int vc6_submit_job(void *handle, const unsigned char *in, unsigned char *out,
                   size_t len, const unsigned char *key,
                   const unsigned char *iv) {
  Batcher *batcher = (Batcher *)handle;
  return batcher->submit(in, out, len, key, iv) ? 1 : 0;
}
}
