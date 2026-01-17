#pragma once

#include <stdexcept>
#include <vector>
#include <vulkan/vulkan.h>

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter,
                        VkMemoryPropertyFlags properties);

struct RingBuffer {
  VkBuffer buffer;
  VkDeviceMemory memory;
  void *mappedUrl;
  VkDeviceSize size;
  VkDeviceSize offset; // Current write head

  // Create synchronization structures here if needed, or in the batcher
};

void createBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
                  VkDeviceSize size, VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags properties, VkBuffer &buffer,
                  VkDeviceMemory &bufferMemory);
