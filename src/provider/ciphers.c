#include <openssl/core_dispatch.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <string.h>

// External C-API from backend
extern void *vc6_init();
extern void vc6_cleanup(void *handle);
extern int vc6_submit_job(void *handle, const unsigned char *in,
                          unsigned char *out, size_t len,
                          const unsigned char *key, const unsigned char *iv);

// Global backend handle for this provider instance
// In a real provider, this should be stored in the provider context
static void *inner_backend = NULL;

typedef struct {
  unsigned char key[32];
  unsigned char iv[16];
  int set_key;
  int set_iv;
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
  /* printf("DEBUG: vc6_aes_init key=%p keylen=%zu iv=%p ivlen=%zu\n", key,
   * keylen, iv, ivlen); */
  if (key != NULL) {
    if (keylen != 16 && keylen != 32) {
      fprintf(stderr, "DEBUG: vc6_aes_init failed keylen=%zu\n", keylen);
      return 0; // Only supporting 128/256 roughly
    }
    memcpy(ctx->key, key, keylen);
    ctx->set_key = 1;
  }
  if (iv != NULL) {
    memcpy(ctx->iv, iv, ivlen);
    ctx->set_iv = 1;
  }
  return 1;
}

static int vc6_aes_final(void *vctx, unsigned char *out, size_t *outl,
                         size_t outsize) {
  *outl = 0;
  return 1;
}

static int vc6_aes_cipher(void *vctx, unsigned char *out, size_t *outl,
                          size_t outsize, const unsigned char *in, size_t inl) {
  VC6_AES_CTX *ctx = (VC6_AES_CTX *)vctx;

  // Just pass to backend
  if (!inner_backend)
    return 0;

  vc6_submit_job(inner_backend, in, out, inl, ctx->key, ctx->iv);
  *outl = inl;

  return 1;
}

static int vc6_aes_get_ctx_params(void *vctx, OSSL_PARAM params[]) {
  VC6_AES_CTX *ctx = (VC6_AES_CTX *)vctx;
  OSSL_PARAM *p;

  p = OSSL_PARAM_locate(params, OSSL_CIPHER_PARAM_KEYLEN);
  if (p != NULL && !OSSL_PARAM_set_size_t(
                       p, 32)) // Defaulting to 32 for now, should store in ctx
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
    if (!OSSL_PARAM_get_size_t(p, &keylen)) // Verify type
      return 0;
    // In reality we should check if keylen matches expectations
    if (keylen != 16 && keylen != 32)
      return 0;
  }

  // We can ignore IVLEN setting for now as we hardcode support
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
