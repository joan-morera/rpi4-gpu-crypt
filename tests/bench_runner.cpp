#include "../src/backend/vulkan_ctx.hpp"
#include "../src/scheduler/batcher.hpp"
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>

int main() {
  try {
    std::cout << "[Bench] Initializing Vulkan Context..." << std::endl;
    VulkanContext ctx;

    std::cout << "[Bench] Initializing Batcher..." << std::endl;
    Batcher batcher(&ctx);

    // Parameters
    const size_t PACKET_SIZE =
        1024 * 1024; // 1MB (Larger batches = Less driver overhead)
    const size_t TOTAL_DATA = 1024ULL * 1024ULL * 1024ULL; // 1GB
    const size_t ITERATIONS = TOTAL_DATA / PACKET_SIZE;

    std::vector<unsigned char> input(PACKET_SIZE, 0xAB);
    std::vector<unsigned char> output(PACKET_SIZE);
    unsigned char key[32] = {0};
    unsigned char iv[16] = {0};

    // Run for both algorithms
    const char *algNames[] = {"AES-128-CTR", "ChaCha20"};
    Batcher::Algorithm algs[] = {Batcher::ALG_AES_CTR, Batcher::ALG_CHACHA20};

    // Test sizes: 1MB (Optimal) and 16KB (OpenSSL default)
    size_t testSizes[] = {1024 * 1024, 16 * 1024};
    const char *sizeNames[] = {"1 MB", "16 KB"};

    for (int s = 0; s < 2; s++) {
      size_t currentPacketSize = testSizes[s];
      size_t currentIterations = TOTAL_DATA / currentPacketSize;

      std::cout << "\n================================================"
                << std::endl;
      std::cout << "Testing with Packet Size: " << sizeNames[s] << std::endl;
      std::cout << "================================================"
                << std::endl;

      for (int a = 0; a < 2; a++) {
        std::cout << "\n[Bench] Testing Algorithm: " << algNames[a]
                  << std::endl;
        std::cout << "[Bench] Iterations: " << currentIterations << std::endl;

        auto start = std::chrono::high_resolution_clock::now();

        for (size_t i = 0; i < currentIterations; i++) {
          if (!batcher.submit(input.data(), output.data(), currentPacketSize,
                              key, iv, algs[a])) {
            std::cerr << "[Bench] Failed at iteration " << i << std::endl;
            return 1;
          }
          // Adaptive logging:
          // 1MB loop (1024 iters) -> log every 20 iters (~1 sec)
          // 16KB loop (65536 iters) -> log every 2000 iters (~1 sec)
          int logInterval = (currentPacketSize > 100000) ? 20 : 2000;

          if (i % logInterval == 0) {
            std::cout << "Iter: " << i << "\r" << std::flush;
          }
        }

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = end - start;

        double mb = (double)TOTAL_DATA / (1024.0 * 1024.0);
        double throughput = mb / diff.count();

        std::cout << "\n[Bench] Completed in " << std::fixed
                  << std::setprecision(3) << diff.count() << " seconds."
                  << std::endl;
        std::cout << "[Bench] Throughput: " << throughput << " MB/s"
                  << std::endl;
      }
    }

  } catch (const std::exception &e) {
    std::cerr << "[Bench] Exception: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
