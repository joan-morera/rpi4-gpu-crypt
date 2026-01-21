# Changelog

All notable fixes and debugging history for the rpi4-gpu-crypt project.

## [2026-01-21] ChaCha20 and Provider Fixes

### Fixed: ChaCha20 Failing with Large Data (100KB+)
**Symptom:** ChaCha20 encryption worked for 64 bytes but failed with larger data sizes.

**Root Cause:** The ChaCha20 implementation was incorrectly using `vc6_aes_final()` which:
1. Cast `VC6_CHACHA_CTX*` to `VC6_AES_CTX*` (wrong struct)
2. Called `vc6_submit_job` with AES algorithm ID (0/1) instead of ChaCha20 (2)
3. Lacked partial block buffering for data sizes not divisible by 64 bytes

**Fix:** 
- Added `partial_buf[64]` and `partial_len` to `VC6_CHACHA_CTX`
- Created proper `vc6_chacha20_final()` function
- Updated `vc6_chacha20_cipher()` to handle partial blocks like AES

**Commit:** `d353c69` - fix: Add partial block buffering and proper final function for ChaCha20

---

### Fixed: AES-256-CTR Wrong Dispatch Table
**Symptom:** AES-256-CTR produced incorrect output.

**Root Cause:** `entrypoint.c` registered AES-256-CTR with `vc6_aes128ctr_functions` instead of `vc6_aes256ctr_functions`, causing the wrong `get_params` function to be used.

**Fix:** Added `extern` declaration for `vc6_aes256ctr_functions` and updated registration.

**Commit:** `95b11b0` - fix: Register AES-256-CTR with correct dispatch table in entrypoint.c

---

### Known Issue: AES-256-CTR Hardcoded for 10 Rounds
**Status:** Not yet fixed.

**Issue:** The `aes_ctr.comp` shader is hardcoded for 10 rounds (AES-128), but AES-256 requires 14 rounds and 60 round key words (vs 44 for AES-128).

**Location:**
- `src/shaders/aes_ctr.comp` line 168: `for (uint r=1; r < 10; r++)`
- `src/scheduler/batcher.cpp`: Key expansion only creates 44 words

---

## [2026-01-21] Test Suite Cleanup

### Removed Obsolete Tests
- `tests/test_sbox.sh` - Obsolete S-Box debugging script
- `tests/verify_real_world.sh` - 50MB ZIP test was overkill

### Retained Essential Tests
- `tests/test_all_ciphers.sh` - Main correctness test (GPU encrypt → CPU decrypt)
- `tests/debug_check.sh` - Byte-by-byte KAT comparison
- `tests/bench_runner.cpp` - Performance benchmark

**Commit:** `43ac827` - chore: Remove obsolete test files

---

## Historical V3D Driver Issues

### V3D SSBO Bug (Mesa)
**Issue:** SSBO reads from workgroups > 0 return corrupted data.

**Workaround:** All shaders use `local_size_x = 256` to ensure data fits within workgroup 0.

**Impact:** Limits maximum batch size per dispatch to 256 blocks (4KB for AES, 16KB for ChaCha20).
