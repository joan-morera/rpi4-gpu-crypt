#!/bin/bash
set -e

# Generate dummy file
dd if=/dev/zero of=input.bin bs=1M count=10

# Encrypt (using our SBox debug shader)
openssl enc -aes-128-ctr -provider vc6 -propquery provider=vc6 -in input.bin -out output.bin -K 000102030405060708090a0b0c0d0e0f -iv 000102030405060708090a0b0c0d0e0f

# Analyze Output
echo "Analyzing output blocks..."
hexdump -v -e '16/1 "%02x " "\n"' output.bin | uniq -c | head -n 20
