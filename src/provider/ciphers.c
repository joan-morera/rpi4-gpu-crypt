#include <openssl/core_dispatch.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <string.h>

// External C-API from backend
extern void *vc6_init();
extern void vc6_cleanup(void *handle);
extern int vc6_submit_job(void *handle, const unsigned char *in,
                          unsigned char *out, size_t len,
                          const unsigned char *key, const unsigned char *iv,
                          int alg_id);

// Global backend handle for this provider instance
static void *inner_backend = NULL;

typedef struct {
  unsigned char key[32];
  unsigned char iv[16];
  int set_key;
  int set_iv;
  size_t key_len; // 16 for AES-128, 32 for AES-256
  // Partial block buffering for stream continuity
  unsigned char partial_buf[16];
  size_t partial_len;
} VC6_AES_CTX;

static void *vc6_aes_newctx(void *provctx) {
  (void)provctx; // Unused
  if (!inner_backend)
    inner_backend = vc6_init();

  VC6_AES_CTX *ctx = (VC6_AES_CTX *)OPENSSL_zalloc(sizeof(*ctx));
  return ctx;
}

static void vc6_aes_freectx(void *vctx) {
  VC6_AES_CTX *ctx = (VC6_AES_CTX *)vctx;
  OPENSSL_free(ctx);
}

#include <stdio.h>

static int vc6_aes_init(void *vctx, const unsigned char *key, size_t keylen,
                        const unsigned char *iv, size_t ivlen,
                        const OSSL_PARAM param[]) {
  VC6_AES_CTX *ctx = (VC6_AES_CTX *)vctx;
  if (key != NULL) {
    if (keylen != 16 && keylen != 32) {
      return 0;
    }
    memcpy(ctx->key, key, keylen);
    ctx->key_len = keylen;
    ctx->set_key = 1;
  }
  if (iv != NULL) {
    memcpy(ctx->iv, iv, ivlen);
    ctx->set_iv = 1;
  }
  ctx->partial_len = 0;
  return 1;
}

static int vc6_aes_final(void *vctx, unsigned char *out, size_t *outl,
                         size_t outsize) {
  VC6_AES_CTX *ctx = (VC6_AES_CTX *)vctx;
  *outl = 0;

  if (ctx->partial_len > 0) {
    if (!inner_backend)
      return 0;

    // Zero pad the rest of the block
    for (size_t i = ctx->partial_len; i < 16; i++) {
      ctx->partial_buf[i] = 0;
    }

    // Encrypt 1 block in-place (in partial_buf)
    // Note: We use the current IV.
    int alg_id = (ctx->key_len == 32) ? 1 : 0;
    int res = vc6_submit_job(inner_backend, ctx->partial_buf, ctx->partial_buf,
                             16, ctx->key, ctx->iv, alg_id);
    if (!res)
      return 0;

    // Copy only the valid bytes to 'out'
    if (outsize < ctx->partial_len)
      return 0; // Error: output buffer too small

    memcpy(out, ctx->partial_buf, ctx->partial_len);
    *outl = ctx->partial_len;
  }
  return 1;
}

// AES Key Expansion (Simple implementation or use OpenSSL's)
// AES-128 needs 11 round keys (176 bytes).
// We will simply use OpenSSL's AES_set_encrypt_key to get the schedule!
#include <openssl/aes.h>

// Helper to increment 128-bit counter by 'blocks'
static void inc_128_counter(unsigned char *counter, size_t blocks) {
  // Treat counter as Big-Endian 128-bit integer
  for (int i = 15; i >= 0; i--) {
    unsigned int sum = counter[i] + (blocks & 0xFF);
    counter[i] = sum & 0xFF;
    blocks >>= 8;
    blocks += (sum >> 8);
    if (blocks == 0)
      break;
  }
}

static int vc6_aes_cipher(void *vctx, unsigned char *out, size_t *outl,
                          size_t outsize, const unsigned char *in, size_t inl) {
  VC6_AES_CTX *ctx = (VC6_AES_CTX *)vctx;
  if (!inner_backend)
    return 0;

  *outl = 0;
  size_t total_written = 0;

  // 1. Handle existing partial buffer
  if (ctx->partial_len > 0) {
    while (inl > 0 && ctx->partial_len < 16) {
      ctx->partial_buf[ctx->partial_len++] = *in++;
      inl--;
    }

    // If full, encrypt it
    if (ctx->partial_len == 16) {
      // Encrypt 1 block (alg: 0=AES-128, 1=AES-256)
      int alg_id = (ctx->key_len == 32) ? 1 : 0;
      int res = vc6_submit_job(inner_backend, ctx->partial_buf, out, 16,
                               ctx->key, ctx->iv, alg_id);
      if (!res)
        return 0;

      out += 16;
      total_written += 16;
      ctx->partial_len = 0;

      // Increment IV by 1
      inc_128_counter(ctx->iv, 1);
    }
  }

  // 2. Process Full Blocks from Input
  if (inl >= 16) {
    size_t full_blocks_len = inl & ~0xF; // Multiple of 16
    int alg_id = (ctx->key_len == 32) ? 1 : 0;

    int res = vc6_submit_job(inner_backend, in, out, full_blocks_len, ctx->key,
                             ctx->iv, alg_id);
    if (!res)
      return 0;

    out += full_blocks_len;
    in += full_blocks_len;
    total_written += full_blocks_len;
    inl -= full_blocks_len;

    // Increment IV by block count
    inc_128_counter(ctx->iv, full_blocks_len / 16);
  }

  // 3. Buffer remaining bytes
  if (inl > 0) {
    for (size_t i = 0; i < inl; i++) {
      ctx->partial_buf[ctx->partial_len++] = in[i];
    }
  }

  *outl = total_written;
  return 1;
}

static int vc6_aes_get_ctx_params(void *vctx, OSSL_PARAM params[]) {
  VC6_AES_CTX *ctx = (VC6_AES_CTX *)vctx;
  OSSL_PARAM *p;

  p = OSSL_PARAM_locate(params, OSSL_CIPHER_PARAM_KEYLEN);
  if (p != NULL && !OSSL_PARAM_set_size_t(p, 32))
    return 0;

  p = OSSL_PARAM_locate(params, OSSL_CIPHER_PARAM_IVLEN);
  if (p != NULL && !OSSL_PARAM_set_size_t(p, 16))
    return 0;

  return 1;
}

static int vc6_aes128_get_params(OSSL_PARAM params[]) {
  OSSL_PARAM *p;
  p = OSSL_PARAM_locate(params, OSSL_CIPHER_PARAM_BLOCK_SIZE);
  if (p != NULL && !OSSL_PARAM_set_size_t(p, 16))
    return 0;
  p = OSSL_PARAM_locate(params, OSSL_CIPHER_PARAM_KEYLEN);
  if (p != NULL && !OSSL_PARAM_set_size_t(p, 16))
    return 0;
  p = OSSL_PARAM_locate(params, OSSL_CIPHER_PARAM_IVLEN);
  if (p != NULL && !OSSL_PARAM_set_size_t(p, 16))
    return 0;
  return 1;
}

static int vc6_aes256_get_params(OSSL_PARAM params[]) {
  OSSL_PARAM *p;
  p = OSSL_PARAM_locate(params, OSSL_CIPHER_PARAM_BLOCK_SIZE);
  if (p != NULL && !OSSL_PARAM_set_size_t(p, 16))
    return 0;
  p = OSSL_PARAM_locate(params, OSSL_CIPHER_PARAM_KEYLEN);
  if (p != NULL && !OSSL_PARAM_set_size_t(p, 32))
    return 0;
  p = OSSL_PARAM_locate(params, OSSL_CIPHER_PARAM_IVLEN);
  if (p != NULL && !OSSL_PARAM_set_size_t(p, 16))
    return 0;
  return 1;
}

static int vc6_aes_set_ctx_params(void *vctx, const OSSL_PARAM params[]) {
  VC6_AES_CTX *ctx = (VC6_AES_CTX *)vctx;
  const OSSL_PARAM *p;

  p = OSSL_PARAM_locate_const(params, OSSL_CIPHER_PARAM_KEYLEN);
  if (p != NULL) {
    size_t keylen;
    if (!OSSL_PARAM_get_size_t(p, &keylen))
      return 0;
    if (keylen != 16 && keylen != 32)
      return 0;
  }
  return 1;
}

static const OSSL_PARAM vc6_aes_known_gettable_params[] = {
    OSSL_PARAM_size_t(OSSL_CIPHER_PARAM_KEYLEN, NULL),
    OSSL_PARAM_size_t(OSSL_CIPHER_PARAM_IVLEN, NULL),
    OSSL_PARAM_size_t(OSSL_CIPHER_PARAM_BLOCK_SIZE, NULL), OSSL_PARAM_END};

static const OSSL_PARAM *vc6_aes_gettable_ctx_params(void *cctx,
                                                     void *provctx) {
  return vc6_aes_known_gettable_params;
}

static const OSSL_PARAM vc6_aes_known_settable_params[] = {
    OSSL_PARAM_size_t(OSSL_CIPHER_PARAM_KEYLEN, NULL),
    OSSL_PARAM_size_t(OSSL_CIPHER_PARAM_IVLEN, NULL), OSSL_PARAM_END};

static const OSSL_PARAM *vc6_aes_settable_ctx_params(void *cctx,
                                                     void *provctx) {
  return vc6_aes_known_settable_params;
}

const OSSL_DISPATCH vc6_aes128ctr_functions[] = {
    {OSSL_FUNC_CIPHER_NEWCTX, (void (*)(void))vc6_aes_newctx},
    {OSSL_FUNC_CIPHER_FREECTX, (void (*)(void))vc6_aes_freectx},
    {OSSL_FUNC_CIPHER_ENCRYPT_INIT, (void (*)(void))vc6_aes_init},
    {OSSL_FUNC_CIPHER_DECRYPT_INIT, (void (*)(void))vc6_aes_init},
    {OSSL_FUNC_CIPHER_UPDATE, (void (*)(void))vc6_aes_cipher},
    {OSSL_FUNC_CIPHER_FINAL, (void (*)(void))vc6_aes_final},
    {OSSL_FUNC_CIPHER_GET_PARAMS, (void (*)(void))vc6_aes128_get_params},
    {OSSL_FUNC_CIPHER_GET_CTX_PARAMS, (void (*)(void))vc6_aes_get_ctx_params},
    {OSSL_FUNC_CIPHER_SET_CTX_PARAMS, (void (*)(void))vc6_aes_set_ctx_params},
    {OSSL_FUNC_CIPHER_GETTABLE_CTX_PARAMS,
     (void (*)(void))vc6_aes_gettable_ctx_params},
    {OSSL_FUNC_CIPHER_SETTABLE_CTX_PARAMS,
     (void (*)(void))vc6_aes_settable_ctx_params},
    {0, NULL}};

const OSSL_DISPATCH vc6_aes256ctr_functions[] = {
    {OSSL_FUNC_CIPHER_NEWCTX, (void (*)(void))vc6_aes_newctx},
    {OSSL_FUNC_CIPHER_FREECTX, (void (*)(void))vc6_aes_freectx},
    {OSSL_FUNC_CIPHER_ENCRYPT_INIT, (void (*)(void))vc6_aes_init},
    {OSSL_FUNC_CIPHER_DECRYPT_INIT, (void (*)(void))vc6_aes_init},
    {OSSL_FUNC_CIPHER_UPDATE, (void (*)(void))vc6_aes_cipher},
    {OSSL_FUNC_CIPHER_FINAL, (void (*)(void))vc6_aes_final},
    {OSSL_FUNC_CIPHER_GET_PARAMS, (void (*)(void))vc6_aes256_get_params},
    {OSSL_FUNC_CIPHER_GET_CTX_PARAMS, (void (*)(void))vc6_aes_get_ctx_params},
    {OSSL_FUNC_CIPHER_SET_CTX_PARAMS, (void (*)(void))vc6_aes_set_ctx_params},
    {OSSL_FUNC_CIPHER_GETTABLE_CTX_PARAMS,
     (void (*)(void))vc6_aes_gettable_ctx_params},
    {OSSL_FUNC_CIPHER_SETTABLE_CTX_PARAMS,
     (void (*)(void))vc6_aes_settable_ctx_params},
    {0, NULL}};

// --- ChaCha20 Implementation ---

// Similar context
typedef struct {
  unsigned char key[32];
  unsigned char iv[16];
  int set_key;
  int set_iv;
} VC6_CHACHA_CTX;

static void *vc6_chacha20_newctx(void *provctx) {
  (void)provctx;
  if (!inner_backend)
    inner_backend = vc6_init();
  return OPENSSL_zalloc(sizeof(VC6_CHACHA_CTX));
}

static void vc6_chacha20_freectx(void *vctx) { OPENSSL_free(vctx); }

static int vc6_chacha20_init(void *vctx, const unsigned char *key,
                             size_t keylen, const unsigned char *iv,
                             size_t ivlen, const OSSL_PARAM params[]) {
  VC6_CHACHA_CTX *ctx = (VC6_CHACHA_CTX *)vctx;
  if (key != NULL) {
    if (keylen != 32)
      return 0;
    memcpy(ctx->key, key, 32);
    ctx->set_key = 1;
  }
  if (iv != NULL) {
    // ChaCha20 IV usually 16 bytes in OpenSSL (contains counter + nonce)
    // Or 12 byte nonce + 4 byte counter.
    // We copy what we get. Max 16.
    if (ivlen > 16)
      ivlen = 16;
    memcpy(ctx->iv, iv, ivlen);
    ctx->set_iv = 1;
  }
  return 1;
}

static int vc6_chacha20_cipher(void *vctx, unsigned char *out, size_t *outl,
                               size_t outsize, const unsigned char *in,
                               size_t inl) {
  VC6_CHACHA_CTX *ctx = (VC6_CHACHA_CTX *)vctx;
  if (!inner_backend)
    return 0;

  // ALG_CHACHA20 = 1
  int res = vc6_submit_job(inner_backend, in, out, inl, ctx->key, ctx->iv, 1);
  if (!res) {
    fprintf(stderr,
            "[VC6-Provider] Error: vc6_submit_job failed for ChaCha20.\n");
    return 0;
  }

  // CRITICAL FIX: Increment the Counter (bytes 0-3 of IV) for next batch
  // ChaCha20 Counter is Little-Endian 32-bit at IV[0..3]
  size_t blocks = (inl + 63) / 64; // Number of 64-byte blocks
  uint32_t counter;
  memcpy(&counter, ctx->iv, 4); // Read LE counter
  counter += blocks;
  memcpy(ctx->iv, &counter, 4); // Write back

  *outl = inl;
  return 1;
}

static int vc6_chacha20_get_params(OSSL_PARAM params[]) {
  OSSL_PARAM *p;
  p = OSSL_PARAM_locate(params, OSSL_CIPHER_PARAM_BLOCK_SIZE);
  if (p != NULL && !OSSL_PARAM_set_size_t(p, 1)) // Steam cipher, block size 1?
    return 0;
  p = OSSL_PARAM_locate(params, OSSL_CIPHER_PARAM_KEYLEN);
  if (p != NULL && !OSSL_PARAM_set_size_t(p, 32))
    return 0;
  p = OSSL_PARAM_locate(params, OSSL_CIPHER_PARAM_IVLEN);
  if (p != NULL && !OSSL_PARAM_set_size_t(p, 16))
    return 0;
  return 1;
}

static const OSSL_PARAM vc6_chacha20_known_gettable_params[] = {
    OSSL_PARAM_size_t(OSSL_CIPHER_PARAM_KEYLEN, NULL),
    OSSL_PARAM_size_t(OSSL_CIPHER_PARAM_IVLEN, NULL),
    OSSL_PARAM_size_t(OSSL_CIPHER_PARAM_BLOCK_SIZE, NULL), OSSL_PARAM_END};

static const OSSL_PARAM *vc6_chacha20_gettable_ctx_params(void *cctx,
                                                          void *provctx) {
  return vc6_chacha20_known_gettable_params;
}

static int vc6_chacha20_get_ctx_params(void *vctx, OSSL_PARAM params[]) {
  OSSL_PARAM *p;
  p = OSSL_PARAM_locate(params, OSSL_CIPHER_PARAM_KEYLEN);
  if (p != NULL && !OSSL_PARAM_set_size_t(p, 32))
    return 0;
  p = OSSL_PARAM_locate(params, OSSL_CIPHER_PARAM_IVLEN);
  if (p != NULL && !OSSL_PARAM_set_size_t(p, 16))
    return 0;
  return 1;
}

static int vc6_chacha20_set_ctx_params(void *vctx, const OSSL_PARAM params[]) {
  return 1; // Stub
}

static const OSSL_PARAM vc6_chacha_known_settable_params[] = {
    OSSL_PARAM_size_t(OSSL_CIPHER_PARAM_KEYLEN, NULL),
    OSSL_PARAM_size_t(OSSL_CIPHER_PARAM_IVLEN, NULL), OSSL_PARAM_END};

static const OSSL_PARAM *vc6_chacha20_settable_ctx_params(void *cctx,
                                                          void *provctx) {
  return vc6_chacha_known_settable_params;
}

const OSSL_DISPATCH vc6_chacha20_functions[] = {
    {OSSL_FUNC_CIPHER_NEWCTX, (void (*)(void))vc6_chacha20_newctx},
    {OSSL_FUNC_CIPHER_FREECTX, (void (*)(void))vc6_chacha20_freectx},
    {OSSL_FUNC_CIPHER_ENCRYPT_INIT, (void (*)(void))vc6_chacha20_init},
    {OSSL_FUNC_CIPHER_DECRYPT_INIT, (void (*)(void))vc6_chacha20_init},
    {OSSL_FUNC_CIPHER_UPDATE, (void (*)(void))vc6_chacha20_cipher},
    {OSSL_FUNC_CIPHER_FINAL,
     (void (*)(void))vc6_aes_final}, // Reuse dummy final
    {OSSL_FUNC_CIPHER_GET_PARAMS, (void (*)(void))vc6_chacha20_get_params},
    {OSSL_FUNC_CIPHER_GET_CTX_PARAMS,
     (void (*)(void))vc6_chacha20_get_ctx_params},
    {OSSL_FUNC_CIPHER_SET_CTX_PARAMS,
     (void (*)(void))vc6_chacha20_set_ctx_params},
    {OSSL_FUNC_CIPHER_GETTABLE_CTX_PARAMS,
     (void (*)(void))vc6_chacha20_gettable_ctx_params},
    {OSSL_FUNC_CIPHER_SETTABLE_CTX_PARAMS,
     (void (*)(void))vc6_chacha20_settable_ctx_params},
    {0, NULL}};
