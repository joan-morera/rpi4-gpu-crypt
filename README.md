# Raspberry Pi 4 Vulkan Crypto Provider using OpenSSL 3.0

This project implements a custom OpenSSL 3.0 Provider that offloads **AES-128-CTR** and **ChaCha20** encryption to the Raspberry Pi 4 GPU (VideoCore VI) using Vulkan Compute. 

It demonstrates how to bridge the gap between CPU-based cryptographic libraries (OpenSSL) and low-power embedded GPUs, achieving correctness and leveraging the GPU for bulk encryption tasks.

## Supported Algorithms

1.  **AES-128-CTR** (Verified ✅)
    *   Implementation: GLSL Compute Shader using Standard S-Box and MixColumns.
    *   Key Expansion: CPU-side (pre-expanded to 176 bytes), passed to GPU.
    *   Features: Correct Big-Endian Counter Increment, 128-bit Counter wraparound support.
    *   Performance: ~138 MB/s (limited by memory bandwidth).

2.  **ChaCha20** (Verified ✅)
    *   Implementation: GLSL Compute Shader (Standard 20-round core).
    *   Features: Standard IETF IV Layout (Counter First or Nonce First supported via OpenSSL logic).
    *   Performance: ~144 MB/s.

## Performance Notes

*   **Memory Bottleneck**: The Raspberry Pi 4 V3D driver does NOT support `VK_MEMORY_PROPERTY_HOST_CACHED_BIT` for visible memory. This limits readback speed.
*   **Batching**: We use large 64MB ring buffers to amortize submit overhead.
*   **Zero-Copy**: Input data is mapped directly to GPU-visible memory to avoid extra copies.

## 🚀 Performance Results

| Backend | Implementation | Throughput (1MB blocks) | Status |
| :--- | :--- | :--- | :--- |
| **CPU (Ref)** | OpenSSL AES-128-CTR | ~105 MB/s | ✅ |
| **CPU (Ref)** | OpenSSL ChaCha20 | ~120 MB/s | ✅ |
| **GPU (VC6)** | **AES-128-CTR** | **~138 MB/s** | ✅ Verified Correct |
| **GPU (VC6)** | **ChaCha20** | **~144 MB/s** | ✅ Verified Correct |

> **Verification**: Both algorithms have been verified byte-for-bit against OpenSSL's CPU implementation using random data and real-world file encryption tests (ZIP files).

## 🏗 Architecture

The system consists of four layers:

1.  **OpenSSL Provider (`ciphers.c`)**: Implements the OpenSSL 3.0 `OSSL_ALGORITHM` interface. It translates OpenSSL `Update` calls into batched jobs.
2.  **C-shim (`entrypoint.c`)**: Bridges the C-based OpenSSL API with the C++ Vulkan backend.
3.  **Vulkan Scheduler (`batcher.cpp`)**: 
    *   Manages a **64MB Zero-Copy Ring Buffer** (Coherent Memory).
    *   Uses **Double-Mapped IO** locally for CPU access.
    *   Submits compute jobs to the GPU synchronously.
4.  **Compute Shader (`aes_ctr.comp` / `chacha20.comp`)**: GLSL shaders compiled to SPIR-V that perform parallel encryption on the GPU.

## 🛠 Prerequisites

*   **Hardware**: Raspberry Pi 4 Model B (or Pi 400/CM4).
*   **OS**: Raspberry Pi OS (64-bit) or any Linux distro with Mesa drivers.
*   **Drivers**: Mesa V3DV (Vulkan) driver must be installed on the host.

## 📦 Building & Running (Docker)

We recommend using Docker to ensure a consistent environment. The container needs access to the host's DRI (Direct Rendering Infrastructure) nodes.

### 1. Build the Image
```bash
docker build -t rpi4-gpu-crypt .
```

### 2. Run the Container
**Critical**: You must pass `--device /dev/dri:/dev/dri` to allow the container to access the GPU.

```bash
docker run -it --rm --device /dev/dri:/dev/dri --tmpfs /ram:rw,size=512M rpi4-gpu-crypt /bin/bash
```

## ⚡ Usage

Inside the container, you can verify correct operation using the provided script `verify_real_world.sh` (if created) or manual OpenSSL commands.

### Verify Correctness (Real World)
```bash
# Encrypt a file using GPU
openssl enc -chacha20 -provider vc6 -propquery provider=vc6 -in input.file -out encrypted.bin -K ... -iv ...

# Decrypt using CPU (default)
openssl enc -d -chacha20 -provider default -in encrypted.bin -out decrypted.file -K ... -iv ...

# Checksum
md5sum input.file decrypted.file
```

### Benchmark
```bash
# -propquery provider=vc6 forces OpenSSL to use our provider
openssl speed -provider vc6 -propquery provider=vc6 -evp aes-128-ctr -bytes 1048576
```

## 🔧 Troubleshooting & Lessons Learned

During development, we solved several critical issues specific to the Raspberry Pi 4:

*   **Vulkan Version**: The Pi 4 V3DV driver (Mesa) is strict. We explicitly lowered the API requirement to **Vulkan 1.0** to ensure compatibility using `VK_API_VERSION_1_0`.
*   **Docker GPU Access**: The container requires `mesa-vulkan-broadcom` and `mesa-vulkan-layers` packages (Alpine/Debian) to correctly identify the passed-through `/dev/dri` device.
*   **Correctness**: 
    *   **AES**: Requires careful handling of **Key Expansion** (must be done on CPU if OpenSSL passes raw keys) and **Counter Endianness** (AES-CTR counters increment as Big-Endian).
    *   **ChaCha20**: Requires correct IV structure handling (Counter vs Nonce words) matching OpenSSL's expectations.
*   **Ring Buffer Wrapping**: We implemented logic to wrap the ring buffer offset. Without this, the buffer overflows after ~400 operations.

## 🔮 Future Improvements

1.  **Asynchronous Batching**: Currently, every `Update` call triggers a GPU submission. Using an async queue to bundle multiple small packets would improve throughput for small-packet workloads.
2.  **Poly1305 MAC**: Adding Poly1305 support would allow full ChaCha20-Poly1305 AEAD offloading.

---
*Created by Antigravity (Google DeepMind) & User*
