# Raspberry Pi 4 Vulkan Crypto Provider for OpenSSL 3.0

GPU-accelerated cryptography for Raspberry Pi 4 using Vulkan Compute shaders, integrated as an OpenSSL 3.0 Provider.

## Supported Algorithms

| Algorithm | Status | Throughput | Notes |
|-----------|--------|------------|-------|
| **AES-256-CTR** | ✅ Verified | ~10 MB/s | 14 rounds, 60 round keys |
| **ChaCha20** | ✅ Verified | ~12 MB/s | Standard IETF layout, 64-byte blocks |

## Quick Start

```bash
# Build
docker build -t rpi4-gpu-crypt .

# Run (must pass GPU device)
docker run -it --rm --device /dev/dri:/dev/dri --tmpfs /ram:rw,size=512M rpi4-gpu-crypt

# Test
./tests/test_all_ciphers.sh
```

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    OpenSSL Application                       │
└─────────────────────────┬───────────────────────────────────┘
                          │ EVP API
┌─────────────────────────▼───────────────────────────────────┐
│              OpenSSL 3.0 Provider (ciphers.c)               │
│  - Partial block buffering for stream continuity            │
│  - Key/IV management                                         │
└─────────────────────────┬───────────────────────────────────┘
                          │ C++ API
┌─────────────────────────▼───────────────────────────────────┐
│              Vulkan Scheduler (batcher.cpp)                  │
│  - 64MB Zero-Copy Ring Buffer                               │
│  - AES-256 Key Expansion                                    │
│  - Memory coherency (Flush/Invalidate)                      │
└─────────────────────────┬───────────────────────────────────┘
                          │ Vulkan Compute
┌─────────────────────────▼───────────────────────────────────┐
│              GPU Compute Shaders (SPIR-V)                    │
│  - aes256_ctr.comp: AES-256-CTR                             │
│  - chacha20.comp: ChaCha20                                  │
└─────────────────────────────────────────────────────────────┘
```

## V3D Driver Workaround

> [!IMPORTANT]
> The Raspberry Pi 4 V3D driver (Mesa) has a bug where SSBO reads from workgroups > 0 return corrupted data.
> 
> **Workaround**: All shaders use `local_size_x = 256` to ensure each OpenSSL chunk (4KB) is processed entirely within workgroup 0.

## Usage Examples

### Encrypt with GPU, Decrypt with CPU
```bash
# Encrypt using GPU provider
openssl enc -aes-256-ctr -provider vc6 -propquery provider=vc6 \
    -in plaintext.bin -out encrypted.bin \
    -K 000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f \
    -iv 000102030405060708090a0b0c0d0e0f

# Decrypt using CPU (verify correctness)
openssl enc -d -aes-256-ctr -provider default \
    -in encrypted.bin -out decrypted.bin \
    -K 000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f \
    -iv 000102030405060708090a0b0c0d0e0f
```

### Benchmark
```bash
openssl speed -provider vc6 -propquery provider=vc6 -evp aes-256-ctr -bytes 1048576
```

## Prerequisites

- **Hardware**: Raspberry Pi 4 Model B (or Pi 400/CM4)
- **Host**: Mesa V3DV Vulkan driver installed
- **Container**: Alpine Linux with `mesa-vulkan-broadcom`

## Troubleshooting

| Issue | Solution |
|-------|----------|
| `VK_ERROR_INITIALIZATION_FAILED` | Ensure `/dev/dri` is passed to container |
| Corruption at byte 1024 | Shader workgroup size must be 256 (V3D bug) |
| Partial block corruption | Provider must buffer non-16-byte-aligned chunks |
| Binary incompatibility | Use matching libc (Alpine/musl or Debian/glibc) |

## Implementation Notes

### AES-256-CTR
- Key expansion performed on CPU
- Counter increments as Big-Endian 128-bit integer
- S-Box stored in SSBO (256 uint32 values)
- 14 rounds, 60 round keys

### ChaCha20
- Standard 20-round quarter-round implementation
- IV layout: `[Counter 4B][Nonce 12B]` (OpenSSL convention)
- Each thread processes one 64-byte block

## License

Apache License 2.0 - See [LICENSE](LICENSE) for details

---
*GPU Crypto Provider for Raspberry Pi 4*
