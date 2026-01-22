#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <stdexcept>
#include <iostream>

class VulkanContext {
public:
    VulkanContext();
    ~VulkanContext();

    VkDevice getDevice() const { return device; }
    VkQueue getComputeQueue() const { return computeQueue; }
    uint32_t getComputeQueueFamilyIndex() const { return computeQueueFamilyIndex; }
    VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }

private:
    VkInstance instance;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device;
    VkQueue computeQueue;
    uint32_t computeQueueFamilyIndex;

    void createInstance();
    void pickPhysicalDevice();
    void createLogicalDevice();
    bool isDeviceSuitable(VkPhysicalDevice device);
};
