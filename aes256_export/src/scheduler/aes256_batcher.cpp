#include "aes256_batcher.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

#define DEBUG_PRINT(fmt, ...)                                                  \
  fprintf(stderr, "[AES256] " fmt "\n", ##__VA_ARGS__)

#define RING_SIZE 1024 * 1024 * 64 // 64MB Ring Buffer

// Standard AES S-Box
static const uint8_t SBOX[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b,
    0xfe, 0xd7, 0xab, 0x76, 0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
    0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26,
    0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2,
    0xeb, 0x27, 0xb2, 0x75, 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
    0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed,
    0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f,
    0x50, 0x3c, 0x9f, 0xa8, 0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
    0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec,
    0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14,
    0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
    0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d,
    0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f,
    0x4b, 0xbd, 0x8b, 0x8a, 0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
    0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e, 0xe1, 0xf8, 0x98, 0x11,
    0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f,
    0xb0, 0x54, 0xbb, 0x16};

AES256Batcher::AES256Batcher(VulkanContext *ctx) : ctx(ctx) {
  DEBUG_PRINT("Initializing AES-256 Batcher...");

  // Create dedicated ring buffers
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

  vkMapMemory(ctx->getDevice(), inputRing.memory, 0, RING_SIZE, 0,
              &inputRing.mappedUrl);
  vkMapMemory(ctx->getDevice(), outputRing.memory, 0, RING_SIZE, 0,
              &outputRing.mappedUrl);

  // Create dedicated param buffer (4KB for params)
  createBuffer(ctx->getDevice(), ctx->getPhysicalDevice(), 4096,
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               paramBuffer, paramMemory);
  vkMapMemory(ctx->getDevice(), paramMemory, 0, 4096, 0, &paramMappedPtr);

  createDescriptors();
  createPipeline();
  createCommandBuffer();
  createSyncObjects();

  DEBUG_PRINT("AES-256 Batcher Initialized Successfully");
}

AES256Batcher::~AES256Batcher() {
  vkDestroyFence(ctx->getDevice(), computeFence, nullptr);
  vkDestroyPipeline(ctx->getDevice(), pipeline, nullptr);
  vkDestroyPipelineLayout(ctx->getDevice(), pipelineLayout, nullptr);
  vkDestroyDescriptorPool(ctx->getDevice(), descriptorPool, nullptr);
  vkDestroyDescriptorSetLayout(ctx->getDevice(), descriptorSetLayout, nullptr);
  vkFreeCommandBuffers(ctx->getDevice(), commandPool, 1, &commandBuffer);
  vkDestroyCommandPool(ctx->getDevice(), commandPool, nullptr);
  vkDestroyBuffer(ctx->getDevice(), paramBuffer, nullptr);
  vkFreeMemory(ctx->getDevice(), paramMemory, nullptr);
  vkDestroyBuffer(ctx->getDevice(), inputRing.buffer, nullptr);
  vkFreeMemory(ctx->getDevice(), inputRing.memory, nullptr);
  vkDestroyBuffer(ctx->getDevice(), outputRing.buffer, nullptr);
  vkFreeMemory(ctx->getDevice(), outputRing.memory, nullptr);
}

bool AES256Batcher::submit(const unsigned char *in, unsigned char *out,
                           size_t len, const unsigned char *key,
                           const unsigned char *iv) {
  if (len > RING_SIZE) {
    DEBUG_PRINT("Error: len %zu > RING_SIZE", len);
    return false;
  }

  // 1. Write input data
  memcpy(inputRing.mappedUrl, in, len);

  // 2. Setup params - AES-256 Extended Layout
  // Layout: batchSize@0, numRounds@4, padding[2]@8-16, RoundKey[60]@16-256
  // IV[4]@256-272, SBox[256]@272
  uint32_t *ubo = (uint32_t *)paramMappedPtr;
  ubo[0] = (len + 15) / 16; // batchSize
  ubo[1] = 14;              // numRounds for AES-256

  // AES-256 Key Expansion (60 words)
  static const uint8_t rcon[15] = {0x01, 0x02, 0x04, 0x08, 0x10,
                                   0x20, 0x40, 0x80, 0x1b, 0x36,
                                   0x6c, 0xd8, 0xab, 0x4d, 0x9a};
  uint32_t w[60];
  memcpy(w, key, 32);
  for (int i = 8; i < 60; i++) {
    uint32_t temp = w[i - 1];
    if (i % 8 == 0) {
      temp = ((temp >> 8) | (temp << 24)); // RotWord
      temp = (SBOX[temp & 0xFF]) | (SBOX[(temp >> 8) & 0xFF] << 8) |
             (SBOX[(temp >> 16) & 0xFF] << 16) |
             (SBOX[(temp >> 24) & 0xFF] << 24); // SubWord
      temp ^= rcon[(i / 8) - 1];
    } else if (i % 8 == 4) {
      // AES-256 extra SubWord step
      temp = (SBOX[temp & 0xFF]) | (SBOX[(temp >> 8) & 0xFF] << 8) |
             (SBOX[(temp >> 16) & 0xFF] << 16) |
             (SBOX[(temp >> 24) & 0xFF] << 24);
    }
    w[i] = w[i - 8] ^ temp;
  }

  // Extended Layout offsets: RoundKey@16, IV@256, SBox@272
  memcpy(ubo + 4, w, 240);  // RoundKey (60 words) at offset 16 bytes
  memcpy(ubo + 64, iv, 16); // IV at offset 256 bytes

  // Upload S-Box at offset 272 bytes
  uint32_t *dstSBox = ubo + 68;
  for (int i = 0; i < 256; i++) {
    dstSBox[i] = (uint32_t)SBOX[i];
  }

  // 3. Record and submit command buffer
  vkResetCommandBuffer(commandBuffer, 0);

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(commandBuffer, &beginInfo);

  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                          pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

  uint32_t blocks = (len + 15) / 16;
  uint32_t groupCount = (blocks + 255) / 256;
  if (groupCount == 0)
    groupCount = 1;

  vkCmdDispatch(commandBuffer, groupCount, 1, 1);
  vkEndCommandBuffer(commandBuffer);

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  vkResetFences(ctx->getDevice(), 1, &computeFence);
  VkResult res =
      vkQueueSubmit(ctx->getComputeQueue(), 1, &submitInfo, computeFence);
  if (res != VK_SUCCESS) {
    DEBUG_PRINT("vkQueueSubmit failed: %d", res);
    return false;
  }

  vkWaitForFences(ctx->getDevice(), 1, &computeFence, VK_TRUE, UINT64_MAX);

  // 4. Copy output
  memcpy(out, outputRing.mappedUrl, len);
  return true;
}

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

static VkShaderModule createShaderModule(VulkanContext *ctx,
                                         const std::vector<char> &code) {
  VkShaderModuleCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = code.size();
  createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

  VkShaderModule module;
  if (vkCreateShaderModule(ctx->getDevice(), &createInfo, nullptr, &module) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create shader module!");
  }
  return module;
}

void AES256Batcher::createDescriptors() {
  // Descriptor set layout
  VkDescriptorSetLayoutBinding bindings[3] = {};
  bindings[0].binding = 0;
  bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  bindings[0].descriptorCount = 1;
  bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

  bindings[1].binding = 1;
  bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  bindings[1].descriptorCount = 1;
  bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

  bindings[2].binding = 2;
  bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  bindings[2].descriptorCount = 1;
  bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

  VkDescriptorSetLayoutCreateInfo layoutInfo = {};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = 3;
  layoutInfo.pBindings = bindings;

  vkCreateDescriptorSetLayout(ctx->getDevice(), &layoutInfo, nullptr,
                              &descriptorSetLayout);

  // Descriptor pool
  VkDescriptorPoolSize poolSize = {};
  poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  poolSize.descriptorCount = 3;

  VkDescriptorPoolCreateInfo poolInfo = {};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = 1;
  poolInfo.pPoolSizes = &poolSize;
  poolInfo.maxSets = 1;

  vkCreateDescriptorPool(ctx->getDevice(), &poolInfo, nullptr, &descriptorPool);

  // Allocate descriptor set
  VkDescriptorSetAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = descriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &descriptorSetLayout;

  vkAllocateDescriptorSets(ctx->getDevice(), &allocInfo, &descriptorSet);

  // Update descriptor set
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
  for (int i = 0; i < 3; i++) {
    writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[i].dstSet = descriptorSet;
    writes[i].dstBinding = i;
    writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[i].descriptorCount = 1;
    writes[i].pBufferInfo = &bufInfo[i];
  }

  vkUpdateDescriptorSets(ctx->getDevice(), 3, writes, 0, nullptr);
}

void AES256Batcher::createPipeline() {
  DEBUG_PRINT("Loading AES-256 shader...");
  auto code = readFile("/usr/local/lib/aes256_ctr.spv");
  VkShaderModule shaderModule = createShaderModule(ctx, code);

  VkPipelineShaderStageCreateInfo shaderStageInfo = {};
  shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  shaderStageInfo.module = shaderModule;
  shaderStageInfo.pName = "main";

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
  DEBUG_PRINT("AES-256 pipeline created");
}

void AES256Batcher::createCommandBuffer() {
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
}

void AES256Batcher::createSyncObjects() {
  VkFenceCreateInfo fenceInfo = {};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  vkCreateFence(ctx->getDevice(), &fenceInfo, nullptr, &computeFence);
}
