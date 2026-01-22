#pragma once

#include "../backend/memory.hpp"
#include "../backend/vulkan_ctx.hpp"
#include <vector>

/**
 * AES128Batcher - Dedicated AES-128-CTR encryption batcher
 *
 * Completely independent implementation with its own Vulkan resources.
 * Uses Original Layout: IV@192, SBox@256
 */
class AES128Batcher {
public:
  AES128Batcher(VulkanContext *ctx);
  ~AES128Batcher();

  bool submit(const unsigned char *in, unsigned char *out, size_t len,
              const unsigned char *key, const unsigned char *iv);

private:
  VulkanContext *ctx;
  RingBuffer inputRing;
  RingBuffer outputRing;

  // Vulkan Objects (dedicated to AES-128)
  VkPipeline pipeline;
  VkPipelineLayout pipelineLayout;
  VkDescriptorSetLayout descriptorSetLayout;
  VkDescriptorPool descriptorPool;
  VkDescriptorSet descriptorSet;
  VkCommandPool commandPool;
  VkCommandBuffer commandBuffer;
  VkFence computeFence;

  // Parameter Buffer
  VkBuffer paramBuffer;
  VkDeviceMemory paramMemory;
  void *paramMappedPtr;

  void createPipeline();
  void createDescriptors();
  void createCommandBuffer();
  void createSyncObjects();
};
