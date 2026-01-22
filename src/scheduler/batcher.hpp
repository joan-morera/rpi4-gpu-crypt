#pragma once

#include "../backend/memory.hpp"
#include "../backend/vulkan_ctx.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

class Batcher {
public:
  Batcher(VulkanContext *ctx);
  ~Batcher();

  // Algorithm IDs for OpenSSL Provider
  enum Algorithm { ALG_AES256_CTR = 0, ALG_CHACHA20 = 1, ALG_COUNT = 2 };

  // Returns true on success, false on error
  bool submit(const unsigned char *in, unsigned char *out, size_t len,
              const unsigned char *key, const unsigned char *iv, Algorithm alg);

private:
  VulkanContext *ctx;
  RingBuffer inputRing;
  RingBuffer outputRing;

  std::thread workerThread;
  std::atomic<bool> running;

  std::mutex queueMutex;
  std::condition_variable queueCv;

  // Simple job tracking for this MVP
  struct PendingJob {
    const unsigned char *in;
    unsigned char *out;
    size_t len;
  };

  void workerLoop();
  void dispatchBatch();

  // Vulkan Objects
  std::vector<VkPipeline> pipelines; // Indexed by Algorithm enum
  VkPipelineLayout pipelineLayout;
  VkDescriptorSetLayout descriptorSetLayout;
  VkDescriptorPool descriptorPool;
  VkDescriptorSet descriptorSet;
  VkCommandPool commandPool;
  std::vector<VkCommandBuffer> commandBuffers;
  VkFence computeFence;

  // Parameter Buffer (UBO)
  VkBuffer paramBuffer;
  VkDeviceMemory paramMemory;
  void *paramMappedUrl;

  void createPipeline();
  void createDescriptors();
  void createCommandBuffers();
  void createSyncObjects();

  uint32_t loadShader(const char *filename, VkShaderModule *module);
};
