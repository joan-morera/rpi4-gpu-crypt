#include "../../include/aes256_gpu.h"
#include <openssl/core.h>
#include <openssl/core_dispatch.h>
#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/params.h>
#include <string.h>

// Provider Context
typedef struct {
  OSSL_LIB_CTX *libctx;
  const OSSL_CORE_HANDLE *handle;
} PROV_CTX;

// Cipher Context
typedef struct {
  void *gpu_ctx;
  unsigned char key[32];
  unsigned char iv[16];
  int key_set;
  int iv_set;
  OSSL_LIB_CTX *libctx;
} AES256_GPU_CTX;

// Forward declarations
static OSSL_FUNC_cipher_newctx_fn aes256_gpu_newctx;
static OSSL_FUNC_cipher_freectx_fn aes256_gpu_freectx;
static OSSL_FUNC_cipher_encrypt_init_fn aes256_gpu_einit;
static OSSL_FUNC_cipher_decrypt_init_fn aes256_gpu_dinit;
static OSSL_FUNC_cipher_update_fn aes256_gpu_update;
static OSSL_FUNC_cipher_final_fn aes256_gpu_final;
static OSSL_FUNC_cipher_get_params_fn aes256_gpu_get_params;
static OSSL_FUNC_cipher_get_ctx_params_fn aes256_gpu_get_ctx_params;
static OSSL_FUNC_cipher_set_ctx_params_fn aes256_gpu_set_ctx_params;

// -------------------------------------------------------------------------
// Cipher Implementation
// -------------------------------------------------------------------------

static void *aes256_gpu_newctx(void *provctx) {
  AES256_GPU_CTX *ctx = OPENSSL_zalloc(sizeof(AES256_GPU_CTX));
  if (ctx == NULL)
    return NULL;

  // Initialize GPU context
  ctx->gpu_ctx = aes256_gpu_init();
  if (ctx->gpu_ctx == NULL) {
    OPENSSL_free(ctx);
    return NULL;
  }

  ctx->libctx = ((PROV_CTX *)provctx)->libctx;
  return ctx;
}

static void aes256_gpu_freectx(void *vctx) {
  AES256_GPU_CTX *ctx = (AES256_GPU_CTX *)vctx;
  if (ctx == NULL)
    return;

  if (ctx->gpu_ctx) {
    aes256_gpu_cleanup(ctx->gpu_ctx);
  }
  OPENSSL_free(ctx);
}

static int aes256_gpu_einit(void *vctx, const unsigned char *key, size_t keylen,
                            const unsigned char *iv, size_t ivlen,
                            const OSSL_PARAM params[]) {
  AES256_GPU_CTX *ctx = (AES256_GPU_CTX *)vctx;

  if (!aes256_gpu_set_ctx_params(ctx, params))
    return 0;

  if (key != NULL) {
    if (keylen != 32)
      return 0;
    memcpy(ctx->key, key, 32);
    ctx->key_set = 1;
  }
  if (iv != NULL) {
    if (ivlen != 16)
      return 0;
    memcpy(ctx->iv, iv, 16);
    ctx->iv_set = 1;
  }
  return 1;
}

// AES-CTR is symmetric, decrypt is same as encrypt
static int aes256_gpu_dinit(void *vctx, const unsigned char *key, size_t keylen,
                            const unsigned char *iv, size_t ivlen,
                            const OSSL_PARAM params[]) {
  return aes256_gpu_einit(vctx, key, keylen, iv, ivlen, params);
}

static int aes256_gpu_update(void *vctx, unsigned char *out, size_t *outl,
                             size_t outsize, const unsigned char *in,
                             size_t inl) {
  AES256_GPU_CTX *ctx = (AES256_GPU_CTX *)vctx;

  if (!ctx->key_set || !ctx->iv_set)
    return 0;
  if (outsize < inl)
    return 0;

  // Direct submission to GPU
  // NOTE: In production, we should buffer small chunks.
  // Assuming GPU backend handles arbitrary sizes via padding/masking or caller
  // sends blocks. But CTR is stream, so bit length matches.

  if (aes256_gpu_encrypt(ctx->gpu_ctx, in, out, inl, ctx->key, ctx->iv)) {
    *outl = inl;
    return 1;
  }
  return 0;
}

static int aes256_gpu_final(void *vctx, unsigned char *out, size_t *outl,
                            size_t outsize) {
  // CTR mode has no padding final block to flush usually, unless buffering.
  *outl = 0;
  return 1;
}

static int aes256_gpu_get_params(OSSL_PARAM params[]) {
  OSSL_PARAM *p;

  p = OSSL_PARAM_locate(params, OSSL_CIPHER_PARAM_BLOCK_SIZE);
  if (p != NULL &&
      !OSSL_PARAM_set_size_t(p, 1)) // CTR is stream => 1 byte block
    return 0;

  p = OSSL_PARAM_locate(params, OSSL_CIPHER_PARAM_KEYLEN);
  if (p != NULL && !OSSL_PARAM_set_size_t(p, 32))
    return 0;

  p = OSSL_PARAM_locate(params, OSSL_CIPHER_PARAM_IVLEN);
  if (p != NULL && !OSSL_PARAM_set_size_t(p, 16))
    return 0;

  return 1;
}

static int aes256_gpu_get_ctx_params(void *vctx, OSSL_PARAM params[]) {
  AES256_GPU_CTX *ctx = (AES256_GPU_CTX *)vctx;
  OSSL_PARAM *p;

  p = OSSL_PARAM_locate(params, OSSL_CIPHER_PARAM_IVLEN);
  if (p != NULL && !OSSL_PARAM_set_size_t(p, 16))
    return 0;

  p = OSSL_PARAM_locate(params, OSSL_CIPHER_PARAM_KEYLEN);
  if (p != NULL && !OSSL_PARAM_set_size_t(p, 32))
    return 0;

  p = OSSL_PARAM_locate(params, OSSL_CIPHER_PARAM_IV);
  if (p != NULL && ctx->iv_set && !OSSL_PARAM_set_octet_string(p, ctx->iv, 16))
    return 0;

  return 1;
}

static int aes256_gpu_set_ctx_params(void *vctx, const OSSL_PARAM params[]) {
  AES256_GPU_CTX *ctx = (AES256_GPU_CTX *)vctx;
  const OSSL_PARAM *p;

  if (params == NULL)
    return 1;

  p = OSSL_PARAM_locate_const(params, OSSL_CIPHER_PARAM_IV);
  if (p != NULL) {
    if (p->data_type == OSSL_PARAM_OCTET_STRING && p->data_size <= 16) {
      memcpy(ctx->iv, p->data, p->data_size);
      ctx->iv_set = 1;
    } else {
      return 0;
    }
  }

  return 1;
}

static const OSSL_DISPATCH aes256_gpu_cipher_funcs[] = {
    {OSSL_FUNC_CIPHER_NEWCTX, (void (*)(void))aes256_gpu_newctx},
    {OSSL_FUNC_CIPHER_FREECTX, (void (*)(void))aes256_gpu_freectx},
    {OSSL_FUNC_CIPHER_ENCRYPT_INIT, (void (*)(void))aes256_gpu_einit},
    {OSSL_FUNC_CIPHER_DECRYPT_INIT, (void (*)(void))aes256_gpu_dinit},
    {OSSL_FUNC_CIPHER_UPDATE, (void (*)(void))aes256_gpu_update},
    {OSSL_FUNC_CIPHER_FINAL, (void (*)(void))aes256_gpu_final},
    {OSSL_FUNC_CIPHER_GET_PARAMS, (void (*)(void))aes256_gpu_get_params},
    {OSSL_FUNC_CIPHER_GET_CTX_PARAMS,
     (void (*)(void))aes256_gpu_get_ctx_params},
    {OSSL_FUNC_CIPHER_SET_CTX_PARAMS,
     (void (*)(void))aes256_gpu_set_ctx_params},
    {0, NULL}};

// -------------------------------------------------------------------------
// Provider Dispatch
// -------------------------------------------------------------------------

static const OSSL_ALGORITHM aes256_gpu_ciphers[] = {
    {"AES-256-CTR", "provider=aes256_gpu", aes256_gpu_cipher_funcs},
    {NULL, NULL, NULL}};

static const OSSL_ALGORITHM *aes256_gpu_query(void *provctx, int operation_id,
                                              int *no_cache) {
  *no_cache = 0;
  switch (operation_id) {
  case OSSL_OP_CIPHER:
    return aes256_gpu_ciphers;
  }
  return NULL;
}

static void aes256_gpu_teardown(void *provctx) {
  PROV_CTX *ctx = (PROV_CTX *)provctx;
  OPENSSL_free(ctx);
}

static const OSSL_DISPATCH aes256_gpu_dispatch_table[] = {
    {OSSL_FUNC_PROVIDER_QUERY_OPERATION, (void (*)(void))aes256_gpu_query},
    {OSSL_FUNC_PROVIDER_TEARDOWN, (void (*)(void))aes256_gpu_teardown},
    {0, NULL}};

int OSSL_provider_init(const OSSL_CORE_HANDLE *handle, const OSSL_DISPATCH *in,
                       const OSSL_DISPATCH **out, void **provctx) {

  PROV_CTX *ctx = OPENSSL_zalloc(sizeof(PROV_CTX));
  if (ctx == NULL)
    return 0;

  ctx->handle = handle;

  // Get LibCtx
  /*
  // If we needed functions from Core, we'd grab them here via 'in'
  */

  *provctx = (void *)ctx;
  *out = aes256_gpu_dispatch_table;
  return 1;
}
