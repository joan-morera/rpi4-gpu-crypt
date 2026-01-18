#include "batcher.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

#define DEBUG_PRINT(fmt, ...) fprintf(stderr, "[VC6] " fmt "\n", ##__VA_ARGS__)

#define RING_SIZE 1024 * 1024 * 64 // 64MB Ring Buffer (Total = 128MB allocated)

Batcher::Batcher(VulkanContext *ctx) : ctx(ctx), running(true) {
  // 1. Create Ring Buffers (Zero Copy)
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

  // Map memory
  vkMapMemory(ctx->getDevice(), inputRing.memory, 0, RING_SIZE, 0,
              &inputRing.mappedUrl);
  vkMapMemory(ctx->getDevice(), outputRing.memory, 0, RING_SIZE, 0,
              &outputRing.mappedUrl);

  // 2. Setup Pipeline Params BUFFER (SSBO)
  // Usage: STORAGE_BUFFER
  createBuffer(ctx->getDevice(), ctx->getPhysicalDevice(), 4096,
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               paramBuffer, paramMemory);
  DEBUG_PRINT("Mapping Memory...");
  vkMapMemory(ctx->getDevice(), paramMemory, 0, 4096, 0, &paramMappedUrl);

  // Upload S-Box (Standard FIPS 197) to Offset 256 (64 uints)
  static const uint32_t sboxTbl[256] = {
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

  // Copy to offset 256 (64 uints)
  memcpy(((char *)paramMappedUrl) + 256, sboxTbl, sizeof(sboxTbl));

  DEBUG_PRINT("Creating Descriptors...");
  createDescriptors();
  DEBUG_PRINT("Creating Pipelines...");
  createPipeline();
  DEBUG_PRINT("Creating Command Buffers...");
  createCommandBuffers();
  DEBUG_PRINT("Creating Sync Objects...");
  createSyncObjects();
  DEBUG_PRINT("Done.");

  DEBUG_PRINT("Batcher Initialized Successfully. Ring Size: %d", RING_SIZE);
}

Batcher::~Batcher() {
  running = false;
  if (workerThread.joinable())
    workerThread.join();

  vkDestroyFence(ctx->getDevice(), computeFence, nullptr);

  for (auto p : pipelines) {
    if (p != VK_NULL_HANDLE)
      vkDestroyPipeline(ctx->getDevice(), p, nullptr);
  }

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

bool Batcher::submit(const unsigned char *in, unsigned char *out, size_t len,
                     const unsigned char *key, const unsigned char *iv,
                     Algorithm alg) {

  if (len > RING_SIZE) {
    DEBUG_PRINT("Error: len %zu > RING_SIZE", len);
    return false;
  }

  if (alg >= ALG_COUNT || pipelines[alg] == VK_NULL_HANDLE) {
    DEBUG_PRINT("Error: Invalid or uninitialized algorithm %d", alg);
    return false;
  }

  // 1. Write Input
  static VkDeviceSize offset = 0;
  if (offset + len > RING_SIZE) {
    offset = 0;
  }

  VkDeviceSize currentInfoOffset = offset;
  memcpy((char *)inputRing.mappedUrl + currentInfoOffset, in, len);

  // 2. Update params
  uint32_t *ubo = (uint32_t *)paramMappedUrl;
  if (alg == ALG_AES_CTR) {
    // FIXED: Round up to nearest block to handle partial blocks!
    ubo[0] = (len + 15) / 16;

    // CRITICAL FIX: OpenSSL passes RAW 16-byte key, NOT expanded round keys!
    // We must expand the key here (AES-128 key expansion).
    static const uint8_t sbox[256] = {
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
    static const uint8_t rcon[10] = {0x01, 0x02, 0x04, 0x08, 0x10,
                                     0x20, 0x40, 0x80, 0x1b, 0x36};

    // AES-128 Key Expansion (key is 16 bytes, output is 44 words)
    uint32_t w[44];
    // Copy initial key (4 words)
    memcpy(w, key, 16);

    for (int i = 4; i < 44; i++) {
      uint32_t temp = w[i - 1];
      if (i % 4 == 0) {
        // RotWord + SubWord + Rcon
        temp =
            ((temp >> 8) |
             (temp << 24)); // RotWord (rotate left 8 bits when viewed as bytes)
        temp = (sbox[temp & 0xFF]) | (sbox[(temp >> 8) & 0xFF] << 8) |
               (sbox[(temp >> 16) & 0xFF] << 16) |
               (sbox[(temp >> 24) & 0xFF] << 24);
        temp ^= rcon[(i / 4) - 1];
      }
      w[i] = w[i - 4] ^ temp;
    }

    // Copy expanded round keys to UBO (no bswap - shader uses LE)
    memcpy(ubo + 4, w, 176);

    // Copy IV (no bswap - already in correct format)
    memcpy(ubo + 48, iv, 16);

    // Upload S-Box (expanded to uint array)
    uint32_t *dstSBox = (uint32_t *)((char *)ubo + 256);
    for (int i = 0; i < 256; i++) {
      dstSBox[i] = (uint32_t)sbox[i];
    }
  } else if (alg == ALG_CHACHA20) {
    // ChaCha 64-byte blocks
    ubo[0] = (len + 63) / 64;
    memcpy(ubo + 4, key, 32);

    // CRITICAL FIX: OpenSSL ChaCha20 IV format is [Counter 4B][Nonce 12B]
    // - IV bytes 0-3: Counter (little-endian)
    // - IV bytes 4-15: Nonce
    // Shader expects: nonce[0..2] = Nonce, nonce[3] = Counter
    memcpy(ubo + 12, iv + 4, 12); // Copy Nonce (IV bytes 4-15) to ubo[12..14]
    memcpy(&ubo[15], iv, 4);      // Copy Counter (IV bytes 0-3) to ubo[15]
  }

  // FORCE FLUSH (Even if Coherent, to be safe on RPi4)
  VkMappedMemoryRange ranges[2] = {};
  ranges[0].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
  ranges[0].memory = inputRing.memory;
  ranges[0].offset = currentInfoOffset; // Or minimal aligned offset
  ranges[0].size = VK_WHOLE_SIZE;       // Flush all for safety/simplicity

  ranges[1].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
  ranges[1].memory = paramMemory;
  ranges[1].offset = 0;
  ranges[1].size = VK_WHOLE_SIZE;

  vkFlushMappedMemoryRanges(ctx->getDevice(), 2, ranges);

  // 3. Update Descriptors
  VkDescriptorBufferInfo bufInfo[3] = {};
  bufInfo[0].buffer = inputRing.buffer;
  bufInfo[0].offset = currentInfoOffset;
  bufInfo[0].range = len;

  bufInfo[1].buffer = outputRing.buffer;
  bufInfo[1].offset = currentInfoOffset;
  bufInfo[1].range = len;

  bufInfo[2].buffer = paramBuffer;
  bufInfo[2].offset = 0;
  bufInfo[2].range = VK_WHOLE_SIZE;

  VkWriteDescriptorSet writes[2] = {};
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

  vkUpdateDescriptorSets(ctx->getDevice(), 2, writes, 0, nullptr);

  offset += len;

  // 4. Record Command Buffer (Dynamic Dispatch)
  // We record every time to ensure Dispatch Size matches workload exactly.
  // This avoids launching 65k groups for small payloads which might choke V3D.
  VkCommandBuffer cb = commandBuffers[alg];
  vkResetCommandBuffer(cb, 0);

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cb, &beginInfo);

  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[alg]);
  vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0,
                          1, &descriptorSet, 0, nullptr);

  // Calculate generic group count
  uint32_t groupCount = 1;
  if (alg == ALG_AES_CTR) {
    // AES: 256 threads per group. Each thread does 1 block?
    // AES shader: global_id * 16. wait.
    // Re-checking AES work distribution.
    // Shader: uint gID = gl_GlobalInvocationID.x;
    // It processes ONE block per thread.
    // Total blocks = len / 16.
    // Total threads needed = len / 16.
    // Groups = (Threads + 255) / 256.
    uint32_t blocks = len / 16;
    groupCount = (blocks + 255) / 256;
  } else {
    // ChaCha: 256 threads per group (workaround for V3D SSBO bug).
    // Each thread processes ONE 64-byte block.
    // Total blocks = (len + 63) / 64.
    // Groups = (Blocks + 255) / 256.
    uint32_t blocks = (len + 63) / 64;
    groupCount = (blocks + 255) / 256;
  }

  // Safety clamp
  if (groupCount == 0)
    groupCount = 1;

  // DEBUG_PRINT("Dispatching %d groups for len %zu", groupCount, len);
  vkCmdDispatch(cb, groupCount, 1, 1);
  vkEndCommandBuffer(cb);

  // 5. Submit
  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cb;

  // DEBUG_PRINT("Submitting Batch...");
  VkResult res =
      vkQueueSubmit(ctx->getComputeQueue(), 1, &submitInfo, computeFence);
  if (res != VK_SUCCESS) {
    DEBUG_PRINT("vkQueueSubmit failed: %d", res);
    return false;
  }

  // 6. Wait
  // DEBUG_PRINT("Waiting for Idle...");
  // Fences seem to hang on V3D with high load. Trying QueueWaitIdle.
  res = vkQueueWaitIdle(ctx->getComputeQueue());
  if (res != VK_SUCCESS) {
    DEBUG_PRINT("vkQueueWaitIdle failed: %d", res);
    return false;
  }

  // FORCE INVALIDATE OUTPUT (Ensure CPU sees GPU writes)
  VkMappedMemoryRange outRange = {};
  outRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
  outRange.memory = outputRing.memory;
  outRange.offset = currentInfoOffset;
  outRange.size = VK_WHOLE_SIZE; // Invalidate all for safety
  vkInvalidateMappedMemoryRanges(ctx->getDevice(), 1, &outRange);

  // 7. Read Output
  // DEBUG_PRINT("Reading Output...");
  memcpy(out, (char *)outputRing.mappedUrl + currentInfoOffset, len);

  return true;
}

// ... helper methods ...

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

  bindings[2].binding = 2; // Params UBO -> SSBO
  bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  bindings[2].descriptorCount = 1;
  bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

  VkDescriptorSetLayoutCreateInfo layoutInfo = {};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = 3;
  layoutInfo.pBindings = bindings;

  vkCreateDescriptorSetLayout(ctx->getDevice(), &layoutInfo, nullptr,
                              &descriptorSetLayout);

  VkDescriptorPoolSize poolSizes[1] = {};
  poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  poolSizes[0].descriptorCount = 3; // 3 SSBOs

  VkDescriptorPoolCreateInfo poolInfo = {};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = 1;
  poolInfo.pPoolSizes = poolSizes;
  poolInfo.maxSets = 1;

  vkCreateDescriptorPool(ctx->getDevice(), &poolInfo, nullptr, &descriptorPool);

  VkDescriptorSetAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = descriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &descriptorSetLayout;

  vkAllocateDescriptorSets(ctx->getDevice(), &allocInfo, &descriptorSet);

  // Initial write (will be updated per frame anyway)
  VkDescriptorBufferInfo bufInfo[3] = {};
  bufInfo[0].buffer = inputRing.buffer;
  bufInfo[0].range = VK_WHOLE_SIZE;
  bufInfo[1].buffer = outputRing.buffer;
  bufInfo[1].range = VK_WHOLE_SIZE;
  bufInfo[2].buffer = paramBuffer;
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
  writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  writes[2].descriptorCount = 1;
  writes[2].pBufferInfo = &bufInfo[2];

  vkUpdateDescriptorSets(ctx->getDevice(), 3, writes, 0, nullptr);
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

void Batcher::createPipeline() {
  pipelines.resize(ALG_COUNT);

  // 1. AES-CTR
  DEBUG_PRINT("Loading AES Shader...");
  auto aesCode = readFile("/usr/local/lib/aes_ctr.spv");
  VkShaderModule aesModule = createShaderModule(ctx, aesCode);
  DEBUG_PRINT("Creating AES Pipeline...");

  VkPipelineShaderStageCreateInfo shaderStageInfo = {};
  shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  shaderStageInfo.module = aesModule;
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
                           nullptr, &pipelines[ALG_AES_CTR]);
  vkDestroyShaderModule(ctx->getDevice(), aesModule, nullptr);
  DEBUG_PRINT("AES Pipeline Created.");

  // 2. ChaCha20
  DEBUG_PRINT("Loading ChaCha20 Shader...");
  try {
    auto chachaCode = readFile("/usr/local/lib/chacha20.spv");
    VkShaderModule chachaModule = createShaderModule(ctx, chachaCode);
    shaderStageInfo.module = chachaModule;
    pipelineInfo.stage = shaderStageInfo;
    DEBUG_PRINT("Creating ChaCha20 Pipeline...");
    vkCreateComputePipelines(ctx->getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo,
                             nullptr, &pipelines[ALG_CHACHA20]);
    vkDestroyShaderModule(ctx->getDevice(), chachaModule, nullptr);
    DEBUG_PRINT("ChaCha20 Pipeline Created.");
  } catch (...) {
    fprintf(stderr, "[VC6] Warning: ChaCha20 shader not found.\n");
  }
}

void Batcher::createCommandBuffers() {
  VkCommandPoolCreateInfo poolInfo = {};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.queueFamilyIndex = ctx->getComputeQueueFamilyIndex();
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

  vkCreateCommandPool(ctx->getDevice(), &poolInfo, nullptr, &commandPool);

  commandBuffers.resize(ALG_COUNT);
  VkCommandBufferAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();
  vkAllocateCommandBuffers(ctx->getDevice(), &allocInfo, commandBuffers.data());

  for (int i = 0; i < ALG_COUNT; i++) {
    VkCommandBuffer cb = commandBuffers[i];
    if (pipelines[i] == VK_NULL_HANDLE)
      continue;

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cb, &beginInfo);

    // We do NOT record here anymore. We record dynamically in submit().
    // vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[i]);
    // vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
    // pipelineLayout,
    //                        0, 1, &descriptorSet, 0, nullptr);
    // Dispatch
    // vkCmdDispatch(cb, 65536, 1, 1);
    // vkEndCommandBuffer(cb);
  }
}

void Batcher::createSyncObjects() {
  VkFenceCreateInfo fenceInfo = {};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = 0;
  vkCreateFence(ctx->getDevice(), &fenceInfo, nullptr, &computeFence);
}

void Batcher::workerLoop() {}
void Batcher::dispatchBatch() {}

// Extern C Interface (Updated)
extern "C" {
void *vc6_init() {
  VulkanContext *ctx = new VulkanContext();
  Batcher *batcher = new Batcher(ctx);
  return (void *)batcher;
}

void vc6_cleanup(void *handle) {
  Batcher *batcher = (Batcher *)handle;
  delete batcher;
}

int vc6_submit_job(void *handle, const unsigned char *in, unsigned char *out,
                   size_t len, const unsigned char *key,
                   const unsigned char *iv, int alg_id) {
  Batcher *batcher = (Batcher *)handle;
  return batcher->submit(in, out, len, key, iv, (Batcher::Algorithm)alg_id) ? 1
                                                                            : 0;
}
}
