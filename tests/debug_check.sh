#!/bin/bash
echo "--- DEBUG: Check AES/ChaCha Output Byte-for-Byte ---"
# Create 64 bytes of Zeros
dd if=/dev/zero of=zeros.bin bs=64 count=1 2>/dev/null

echo "1. CHA CHA 20"
# Key=1, IV=1
K=0101010101010101010101010101010101010101010101010101010101010101
IV=01010101010101010101010101010101

# Run VC6
openssl enc -chacha20 -provider vc6 -propquery provider=vc6 -in zeros.bin -out chacha_vc6.enc -K $K -iv $IV
# Run Ref
openssl enc -chacha20 -provider default -in zeros.bin -out chacha_ref.enc -K $K -iv $IV

echo "--- VC6 Output (First 32 bytes) ---"
hexdump -C chacha_vc6.enc | head -n 2
echo "--- REF Output (First 32 bytes) ---"
hexdump -C chacha_ref.enc | head -n 2

echo ""
echo "2. AES 128 CTR"
# Key=1, IV=1
K_AES=01010101010101010101010101010101
IV_AES=01010101010101010101010101010101

# Run VC6
openssl enc -aes-128-ctr -provider vc6 -propquery provider=vc6 -in zeros.bin -out aes_vc6.enc -K $K_AES -iv $IV_AES
# Run Ref
openssl enc -aes-128-ctr -provider default -in zeros.bin -out aes_ref.enc -K $K_AES -iv $IV_AES

echo "--- VC6 Output (First 32 bytes) ---"
hexdump -C aes_vc6.enc | head -n 2
echo "--- REF Output (First 32 bytes) ---"
hexdump -C aes_ref.enc | head -n 2
