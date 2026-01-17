#!/bin/bash
set -e

echo "=== Real-World Verification: Large ZIP Encryption ==="

# 1. Create a large random file (50MB)
echo "[*] Generating 50MB random data..."
dd if=/dev/urandom of=bigdata.bin bs=1M count=50 2>/dev/null
md5sum bigdata.bin > original.md5
echo "    MD5: $(cat original.md5)"

# 2. Zip it (adds file structure sensitivity)
echo "[*] Zipping data..."
zip input.zip bigdata.bin
rm bigdata.bin

# 3. Encrypt with VC6 (AES-128-CTR)
echo "[*] Encrypting input.zip -> encrypted.bin using VC6 (GPU)..."
start_time=$(date +%s.%N)
openssl enc -aes-128-ctr -provider vc6 -propquery provider=vc6 -in input.zip -out encrypted.bin -K 000102030405060708090a0b0c0d0e0f -iv 000102030405060708090a0b0c0d0e0f
end_time=$(date +%s.%N)
duration=$(echo "$end_time - $start_time" | bc)
echo "    Encryption took $duration seconds."

# 4. Decrypt with CPU (Default)
echo "[*] Decrypting encrypted.bin -> decrypted.zip using CPU..."
openssl enc -d -aes-128-ctr -provider default -in encrypted.bin -out decrypted.zip -K 000102030405060708090a0b0c0d0e0f -iv 000102030405060708090a0b0c0d0e0f

# 5. Verify Structure
echo "[*] Unzipping decrypted.zip..."
unzip -o decrypted.zip

# 6. Verify Integrity
echo "[*] Checking MD5..."
md5sum -c original.md5

if [ $? -eq 0 ]; then
    echo "SUCCESS: File recovered perfectly!"
else
    echo "FAILURE: Checksum mismatch!"
    exit 1
fi
