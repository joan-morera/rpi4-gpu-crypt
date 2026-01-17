# Raspberry Pi 4 Vulkan Crypto Provider using OpenSSL 3.0

This project implements a custom OpenSSL 3.0 Provider that offloads **AES-256-CTR** encryption to the Raspberry Pi 4 GPU (VideoCore VI) using Vulkan Compute. 

It demonstrates how to bridge the gap between CPU-based cryptographic libraries (OpenSSL) and low-power embedded GPUs, achieving performance exceeding the CPU for large data batches.

## 🚀 Performance Results

| Backend | Implementation | Throughput (16KB Blocks) |
|:---|:---|:---|
| **CPU** | OpenSSL Default (ARMv8 NEON) | ~106 MB/s |
| **GPU** | **VC6 Provider (Vulkan)** | **~134 MB/s** |
| **Speedup** | | **~26% Faster** |

## 🏗 Architecture

The system consists of four layers:

1.  **OpenSSL Provider (`ciphers.c`)**: Implements the OpenSSL 3.0 `OSSL_ALGORITHM` interface. It translates OpenSSL `Update` calls into batched jobs.
2.  **C-shim (`entrypoint.c`)**: Bridges the C-based OpenSSL API with the C++ Vulkan backend.
3.  **Vulkan Scheduler (`batcher.cpp`)**: 
    *   Manages a **64MB Zero-Copy Ring Buffer** (Coherent Memory).
    *   Uses **Pre-Recorded Command Buffers** to minimize CPU overhead.
    *   Submits compute jobs to the GPU synchronously (proven most stable for sequential `openssl speed` benchmarks).
4.  **Compute Shader (`aes_ctr.comp`)**: A GLSL shader compiled to SPIR-V that performs parallel AES-256-CTR encryption on the GPU.

## 🛠 Prerequisites

*   **Hardware**: Raspberry Pi 4 Model B (or Pi 400/CM4).
*   **OS**: Raspberry Pi OS (64-bit) or any Linux distro with Mesa drivers.
*   **Drivers**: Mesa V3DV (Vulkan) driver must be installed on the host.

## 📦 Building & Running (Docker)

We recommend using Docker to ensure a consistent environment. The container needs access to the host's DRI (Direct Rendering Infrastructure) nodes.

### 1. Build the Image
```bash
docker build -t rpi4-vulkan-crypto .
```

### 2. Run the Container
**Critical**: You must pass `--device /dev/dri:/dev/dri` to allow the container to access the GPU.

```bash
docker run -it --rm --device /dev/dri:/dev/dri rpi4-vulkan-crypto
```

## ⚡ Usage

Inside the container, you can benchmark the provider against the default CPU implementation.

### Test GPU Provider
```bash
# -propquery provider=vc6 forces OpenSSL to use our provider
openssl speed -provider vc6 -propquery provider=vc6 -evp aes-256-ctr -bytes 16384 -seconds 10
```

### Test CPU (Default)
```bash
openssl speed -provider default -evp aes-256-ctr -bytes 16384 -seconds 10
```

## 🔧 Troubleshooting & Lessons Learned

During development, we solved several critical issues specific to the Raspberry Pi 4:

*   **Vulkan Version**: The Pi 4 V3DV driver (Mesa) is strict. We explicitly lowered the API requirement to **Vulkan 1.0** to ensure compatibility using `VK_API_VERSION_1_0`.
*   **Docker GPU Access**: The container requires `mesa-vulkan-broadcom` and `mesa-vulkan-layers` packages (Alpine Linux) to correctly identify the passed-through `/dev/dri` device. Without these, you get `VK_ERROR_INCOMPATIBLE_DRIVER (-9)`.
*   **OpenSSL Parameters**: OpenSSL 3.0 requires providers to implement `Get/Set Context Params` tables. If missing, it fails with "Failed to set key".
*   **Ring Buffer Wrapping**: We implemented logic to wrap the ring buffer offset. Without this, the buffer overflows after ~400 operations, causing a silent crash or visual corruption.

## 🔮 Future Improvements

1.  **Asynchronous Batching**: Currently, every `Update` call triggers a GPU submission. For small packets (like WireGuard's 1.4KB), this is inefficient. Using an async queue to bundle multiple small packets into one GPU dispatch would drastically improve throughput for network traffic.
2.  **ChaCha20-Poly1305**: The GPU is exceptionally well-suited for ChaCha20. Implementing this would allow accelerating WireGuard traffic using userspace tools (e.g., `wireguard-go`).

---
*Created by Antigravity (Google DeepMind) & User*
