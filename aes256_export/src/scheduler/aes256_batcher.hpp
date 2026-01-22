#pragma once

#include "../backend/memory.hpp"
#include "../backend/vulkan_ctx.hpp"
#include <vector>

/**
 * AES256Batcher - Dedicated AES-256-CTR encryption batcher
 *
 * Completely independent implementation with its own Vulkan resources.
 * Uses Extended Layout: IV@256, SBox@272
 */
class AES256Batcher {
public:
  AES256Batcher(VulkanContext *ctx);
  ~AES256Batcher();

  bool submit(const unsigned char *in, unsigned char *out, size_t len,
              const unsigned char *key, const unsigned char *iv);

private:
  VulkanContext *ctx;
  RingBuffer inputRing;
  RingBuffer outputRing;

  // Vulkan Objects (dedicated to AES-256)
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
