#ifndef REAL_ROOTS_H
#define REAL_ROOTS_H
#include "mbedtls/x509_crt.h"
#ifdef __cplusplus
extern "C" {
#endif
int real_roots_lookup(void *,const mbedtls_x509_crt *,mbedtls_x509_crt **);
int real_roots_attach(void *);
#ifdef __cplusplus
}
#endif
#endif
