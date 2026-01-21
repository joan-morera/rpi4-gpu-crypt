#include <openssl/core.h>
#include <openssl/core_dispatch.h>
#include <openssl/core_names.h>
#include <stdio.h>
#include <string.h>

extern const OSSL_DISPATCH vc6_aes128ctr_functions[];
extern const OSSL_DISPATCH vc6_aes256ctr_functions[];
extern const OSSL_DISPATCH vc6_chacha20_functions[];

static const OSSL_ALGORITHM vc6_ciphers[] = {
    {"AES-128-CTR", "provider=vc6", vc6_aes128ctr_functions},
    {"AES-256-CTR", "provider=vc6", vc6_aes256ctr_functions},
    {"ChaCha20", "provider=vc6", vc6_chacha20_functions},
    {NULL, NULL, NULL}};


static const OSSL_DISPATCH vc6_query_operation[] = {
    {OSSL_FUNC_PROVIDER_QUERY_OPERATION, (void (*)(void))NULL}, {0, NULL}};

static const OSSL_ALGORITHM *vc6_query(void *provctx, int operation_id,
                                       int *no_cache) {
  *no_cache = 0;
  switch (operation_id) {
  case OSSL_OP_CIPHER:
    return vc6_ciphers;
  }
  return NULL;
}

static void vc6_teardown(void *provctx) {
  // Cleanup global handle if initialized
}

/* Functions we provide to the core */
static const OSSL_DISPATCH vc6_dispatch_table[] = {
    {OSSL_FUNC_PROVIDER_TEARDOWN, (void (*)(void))vc6_teardown},
    {OSSL_FUNC_PROVIDER_QUERY_OPERATION, (void (*)(void))vc6_query},
    {0, NULL}};

/* The entry point */
int OSSL_provider_init(const OSSL_CORE_HANDLE *handle, const OSSL_DISPATCH *in,
                       const OSSL_DISPATCH **out, void **provctx) {
  *out = vc6_dispatch_table;
  *provctx = (void *)handle;
  return 1;
}
