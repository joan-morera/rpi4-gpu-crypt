#!/bin/bash
# Comprehensive test script for all GPU crypto ciphers
set -e

echo "=================================================="
echo "  GPU Crypto Provider - Cipher Verification Suite"
echo "=================================================="

# Use /ram for speed if available, else use /tmp
if [ -d /ram ]; then
    WORKDIR=/ram
else
    WORKDIR=/tmp
fi
cd $WORKDIR
echo "Working in: $WORKDIR"

# Test data
echo "[*] Generating test data..."
dd if=/dev/urandom of=testdata.bin bs=1K count=100 2>/dev/null
TEST_KEY_128="000102030405060708090a0b0c0d0e0f"
TEST_KEY_256="000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
TEST_IV="000102030405060708090a0b0c0d0e0f"

PASSED=0
FAILED=0

run_test() {
    local name=$1
    local cipher=$2
    local key=$3
    local iv=$4
    
    echo ""
    echo "=== Testing $name ==="
    
    # Encrypt with GPU
    if ! openssl enc -$cipher -provider vc6 -propquery provider=vc6 \
        -in testdata.bin -out encrypted.bin -K "$key" -iv "$iv" 2>/dev/null; then
        echo "  [FAIL] GPU encryption failed"
        FAILED=$((FAILED + 1))
        return
    fi
    
    # Decrypt with CPU
    if ! openssl enc -d -$cipher -provider default \
        -in encrypted.bin -out decrypted.bin -K "$key" -iv "$iv" 2>/dev/null; then
        echo "  [FAIL] CPU decryption failed"
        FAILED=$((FAILED + 1))
        return
    fi
    
    # Compare
    if cmp -s testdata.bin decrypted.bin; then
        echo "  [PASS] GPU encrypt + CPU decrypt matches original"
        PASSED=$((PASSED + 1))
    else
        echo "  [FAIL] Data mismatch!"
        cmp -l testdata.bin decrypted.bin | head -n 5
        FAILED=$((FAILED + 1))
    fi
    
    rm -f encrypted.bin decrypted.bin
}

# Run tests
run_test "AES-128-CTR" "aes-128-ctr" "$TEST_KEY_128" "$TEST_IV"
run_test "AES-256-CTR" "aes-256-ctr" "$TEST_KEY_256" "$TEST_IV"
run_test "ChaCha20" "chacha20" "$TEST_KEY_256" "$TEST_IV"

# Cleanup
rm -f testdata.bin

echo ""
echo "=================================================="
echo "  Results: $PASSED passed, $FAILED failed"
echo "=================================================="

if [ $FAILED -gt 0 ]; then
    exit 1
fi
exit 0
