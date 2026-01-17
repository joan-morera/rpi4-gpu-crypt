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

static int vc6_aes_init(void *vctx, const unsigned char *params,
                        const unsigned char *key, size_t keylen,
                        const unsigned char *iv, size_t ivlen,
                        const OSSL_PARAM param[]) {
  VC6_AES_CTX *ctx = (VC6_AES_CTX *)vctx;
  if (key != NULL) {
    if (keylen != 16 && keylen != 32)
      return 0; // Only supporting 128/256 roughly
    memcpy(ctx->key, key, keylen);
    ctx->set_key = 1;
  }
  if (iv != NULL) {
    memcpy(ctx->iv, iv, ivlen);
    ctx->set_iv = 1;
  }
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

const OSSL_DISPATCH vc6_aes128ctr_functions[] = {
    {OSSL_FUNC_CIPHER_NEWCTX, (void (*)(void))vc6_aes_newctx},
    {OSSL_FUNC_CIPHER_FREECTX, (void (*)(void))vc6_aes_freectx},
    {OSSL_FUNC_CIPHER_ENCRYPT_INIT, (void (*)(void))vc6_aes_init},
    {OSSL_FUNC_CIPHER_DECRYPT_INIT, (void (*)(void))vc6_aes_init},
    {OSSL_FUNC_CIPHER_UPDATE, (void (*)(void))vc6_aes_cipher},
    {OSSL_FUNC_CIPHER_FINAL,
     (void (*)(void))NULL}, // CTR doesn't have buffering/padding
    {0, NULL}};
