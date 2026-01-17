#include "batcher.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

#define DEBUG_PRINT(fmt, ...) fprintf(stderr, "[VC6] " fmt "\n", ##__VA_ARGS__)

#define RING_SIZE 1024 * 1024 * 64 // 64MB Ring Buffer

Batcher::Batcher(VulkanContext *ctx) : ctx(ctx), running(true) {
  // 1. Create Ring Buffers (HOST_COHERENT = Zero Copy on RPi4)
  createBuffer(ctx->getDevice(), ctx->getPhysicalDevice(), RING_SIZE,
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               inputRing.buffer, inputRing.memory);

  createBuffer(ctx->getDevice(), ctx->getPhysicalDevice(), RING_SIZE,
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               outputRing.buffer, outputRing.memory);

  // Map memory immediately
  vkMapMemory(ctx->getDevice(), inputRing.memory, 0, RING_SIZE, 0,
              &inputRing.mappedUrl);
  vkMapMemory(ctx->getDevice(), outputRing.memory, 0, RING_SIZE, 0,
              &outputRing.mappedUrl);

  // 2. Setup Vulkan Pipeline
  // Create UBO for params
  createBuffer(ctx->getDevice(), ctx->getPhysicalDevice(), 1024,
               VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               paramBuffer, paramMemory);
  vkMapMemory(ctx->getDevice(), paramMemory, 0, 1024, 0, &paramMappedUrl);

  createDescriptors(); // Now includes binding 2
  createPipeline();
  createCommandBuffers(); // Pre-records commands
  createSyncObjects();

  DEBUG_PRINT("Batcher Initialized Successfully. Ring Size: %d", RING_SIZE);
}

Batcher::~Batcher() {
  running = false;
  queueCv.notify_all();
  if (workerThread.joinable())
    workerThread.join();

  vkDestroyFence(ctx->getDevice(), computeFence, nullptr);
  vkDestroyPipeline(ctx->getDevice(), pipeline, nullptr);
  vkDestroyPipelineLayout(ctx->getDevice(), pipelineLayout, nullptr);
  vkDestroyDescriptorPool(ctx->getDevice(), descriptorPool, nullptr);
  vkDestroyDescriptorSetLayout(ctx->getDevice(), descriptorSetLayout, nullptr);

  vkDestroyBuffer(ctx->getDevice(), paramBuffer, nullptr);
  vkFreeMemory(ctx->getDevice(), paramMemory, nullptr);

  vkDestroyBuffer(ctx->getDevice(), inputRing.buffer, nullptr);
  vkFreeMemory(ctx->getDevice(), inputRing.memory, nullptr);
  vkDestroyBuffer(ctx->getDevice(), outputRing.buffer, nullptr);
  vkFreeMemory(ctx->getDevice(), outputRing.memory, nullptr);
}

#define DEBUG_PRINT(fmt, ...) fprintf(stderr, "[VC6] " fmt "\n", ##__VA_ARGS__)

bool Batcher::submit(const unsigned char *in, unsigned char *out, size_t len,
                     const unsigned char *key, const unsigned char *iv) {
  /* DEBUG_PRINT("submitting batch len=%zu", len); */
  if (len > RING_SIZE) {
    DEBUG_PRINT("Error: len %zu > RING_SIZE", len);
    return false;
  }

  // 1. Write Input (Zero Copy)
  static VkDeviceSize offset = 0;

  // Align offset to 16 bytes (or 256 for optimal GPU access)
  // Input len is 16384 (aligned).

  if (offset + len > RING_SIZE) {
    offset = 0; // Wrap around
  }

  // Check if we are overwriting data that GPU is still using?
  // Since we WaitFences() synchronously at step 4, the GPU is IDLE when we get
  // here. So it's safe to overwrite any part of the buffer.

  // Use the current offset
  VkDeviceSize currentInfoOffset = offset;
  memcpy((char *)inputRing.mappedUrl + currentInfoOffset, in, len);

  // 2. Update params (at offset in param buffer? No, param buffer is small 1024
  // bytes) We use offset=0 for params always (sync execution).

  uint32_t *ubo = (uint32_t *)paramMappedUrl;
  ubo[0] = len / 16;
  memcpy(ubo + 4, key, 32);
  memcpy(ubo + 12, iv, 16);

  // 3. Update Descriptor Set offsets?? NO.
  // We pre-recorded the command buffer with descriptors pointing to base
  // address (offset 0, range WHOLE_SIZE). The SHADER needs to know the offset
  // to read from! OR we update the descriptor set BEFORE submitting to point to
  // the new dynamic offset. Updating descriptor sets is cheap-ish.

  // Pre-recorded command buffer binds the set.
  // If we change the set (DescriptorUpdate), does the pre-recorded command see
  // it? "The descriptor set contents are consumed at execution time". YES.

  VkDescriptorBufferInfo bufInfo[3] = {};
  bufInfo[0].buffer = inputRing.buffer;
  bufInfo[0].offset = currentInfoOffset;
  bufInfo[0].range = len; // Or WHOLE_SIZE - offset

  bufInfo[1].buffer = outputRing.buffer;
  bufInfo[1].offset = currentInfoOffset;
  bufInfo[1].range = len;

  bufInfo[2].buffer = paramBuffer;
  bufInfo[2].offset = 0;
  bufInfo[2].range = VK_WHOLE_SIZE;

  VkWriteDescriptorSet writes[2] = {};
  writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[0].dstSet = descriptorSet;
  writes[0].dstBinding = 0; // Input
  writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  writes[0].descriptorCount = 1;
  writes[0].pBufferInfo = &bufInfo[0];

  writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[1].dstSet = descriptorSet;
  writes[1].dstBinding = 1; // Output
  writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  writes[1].descriptorCount = 1;
  writes[1].pBufferInfo = &bufInfo[1];

  // UPDATE DESCRIPTORS to point to the new sliding window
  vkUpdateDescriptorSets(ctx->getDevice(), 2, writes, 0, nullptr);

  // Advance offset for next batch
  offset += len;

  // 4. Submit
  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  VkResult res =
      vkQueueSubmit(ctx->getComputeQueue(), 1, &submitInfo, computeFence);
  if (res != VK_SUCCESS) {
    DEBUG_PRINT("vkQueueSubmit failed: %d", res);
    return false;
  }

  // 5. Wait
  res =
      vkWaitForFences(ctx->getDevice(), 1, &computeFence, VK_TRUE, UINT64_MAX);
  if (res != VK_SUCCESS) {
    DEBUG_PRINT("vkWaitForFences failed: %d", res);
    return false;
  }

  vkResetFences(ctx->getDevice(), 1, &computeFence);

  // 6. Read Output
  memcpy(out, (char *)outputRing.mappedUrl + currentInfoOffset, len);

  return true;
}

void Batcher::workerLoop() {
  // Unused in synchronous submit mode
}

void Batcher::dispatchBatch() {
  // Unused
}

// --- Init Helpers ---

void Batcher::createDescriptors() {
  VkDescriptorSetLayoutBinding bindings[3] = {};
  bindings[0].binding = 0;
  bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  bindings[0].descriptorCount = 1;
  bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

  bindings[1].binding = 1;
  bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  bindings[1].descriptorCount = 1;
  bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

  bindings[2].binding = 2; // Params UBO
  bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  bindings[2].descriptorCount = 1;
  bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

  VkDescriptorSetLayoutCreateInfo layoutInfo = {};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = 3;
  layoutInfo.pBindings = bindings;

  vkCreateDescriptorSetLayout(ctx->getDevice(), &layoutInfo, nullptr,
                              &descriptorSetLayout);

  VkDescriptorPoolSize poolSizes[2] = {};
  poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  poolSizes[0].descriptorCount = 2;
  poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  poolSizes[1].descriptorCount = 1;

  VkDescriptorPoolCreateInfo poolInfo = {};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = 2;
  poolInfo.pPoolSizes = poolSizes;
  poolInfo.maxSets = 1;

  vkCreateDescriptorPool(ctx->getDevice(), &poolInfo, nullptr, &descriptorPool);

  VkDescriptorSetAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = descriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &descriptorSetLayout;

  vkAllocateDescriptorSets(ctx->getDevice(), &allocInfo, &descriptorSet);

  VkDescriptorBufferInfo bufInfo[3] = {};
  bufInfo[0].buffer = inputRing.buffer;
  bufInfo[0].offset = 0;
  bufInfo[0].range = VK_WHOLE_SIZE;

  bufInfo[1].buffer = outputRing.buffer;
  bufInfo[1].offset = 0;
  bufInfo[1].range = VK_WHOLE_SIZE;

  bufInfo[2].buffer = paramBuffer;
  bufInfo[2].offset = 0;
  bufInfo[2].range = VK_WHOLE_SIZE;

  VkWriteDescriptorSet writes[3] = {};
  writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[0].dstSet = descriptorSet;
  writes[0].dstBinding = 0;
  writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  writes[0].descriptorCount = 1;
  writes[0].pBufferInfo = &bufInfo[0];

  writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[1].dstSet = descriptorSet;
  writes[1].dstBinding = 1;
  writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  writes[1].descriptorCount = 1;
  writes[1].pBufferInfo = &bufInfo[1];

  writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[2].dstSet = descriptorSet;
  writes[2].dstBinding = 2;
  writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  writes[2].descriptorCount = 1;
  writes[2].pBufferInfo = &bufInfo[2];

  vkUpdateDescriptorSets(ctx->getDevice(), 3, writes, 0, nullptr);
}

// Shader loading helper
static std::vector<char> readFile(const std::string &filename) {
  std::ifstream file(filename, std::ios::ate | std::ios::binary);
  if (!file.is_open())
    throw std::runtime_error("failed to open file: " + filename);
  size_t fileSize = (size_t)file.tellg();
  std::vector<char> buffer(fileSize);
  file.seekg(0);
  file.read(buffer.data(), fileSize);
  return buffer;
}

void Batcher::createPipeline() {
  auto code = readFile("/usr/local/lib/aes_ctr.spv");

  VkShaderModuleCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = code.size();
  createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

  VkShaderModule shaderModule;
  vkCreateShaderModule(ctx->getDevice(), &createInfo, nullptr, &shaderModule);

  VkPipelineShaderStageCreateInfo shaderStageInfo = {};
  shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  shaderStageInfo.module = shaderModule;
  shaderStageInfo.pName = "main";

  // No Push Constants anymore for Pipeline Layout (Use UBO)
  VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;

  vkCreatePipelineLayout(ctx->getDevice(), &pipelineLayoutInfo, nullptr,
                         &pipelineLayout);

  VkComputePipelineCreateInfo pipelineInfo = {};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.stage = shaderStageInfo;
  pipelineInfo.layout = pipelineLayout;

  vkCreateComputePipelines(ctx->getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo,
                           nullptr, &pipeline);

  vkDestroyShaderModule(ctx->getDevice(), shaderModule, nullptr);
}

void Batcher::createCommandBuffers() {
  VkCommandPoolCreateInfo poolInfo = {};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.queueFamilyIndex = ctx->getComputeQueueFamilyIndex();
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

  vkCreateCommandPool(ctx->getDevice(), &poolInfo, nullptr, &commandPool);

  VkCommandBufferAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;

  vkAllocateCommandBuffers(ctx->getDevice(), &allocInfo, &commandBuffer);

  // PRE-RECORD COMMANDS (Only Dispatch size needs to be large enough)
  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  vkBeginCommandBuffer(commandBuffer, &beginInfo);

  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                          pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

  // Dispatch Max
  // We dispatch enough groups to cover the Ring Buffer.
  // The shader will check `if (gID >= batchSize) return`.
  // RING_SIZE = 16MB. 64MB hardcoded define?
  // 64MB / 16 bytes per block = 4M blocks.
  // 4M / 64 threads = 65536 groups.
  vkCmdDispatch(commandBuffer, 65536, 1, 1);

  vkEndCommandBuffer(commandBuffer);
}

void Batcher::createSyncObjects() {
  VkFenceCreateInfo fenceInfo = {};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = 0;

  vkCreateFence(ctx->getDevice(), &fenceInfo, nullptr, &computeFence);
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
