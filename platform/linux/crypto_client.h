#ifndef ALLWHITHER_CRYPTO_CLIENT_H
#define ALLWHITHER_CRYPTO_CLIENT_H

#include <JavaScriptCore/JavaScript.h>

typedef struct _CryptoClient CryptoClient;

CryptoClient *crypto_client_new(JSGlobalContextRef context);
JSObjectRef crypto_client_js_object(CryptoClient *client);
void crypto_client_free(CryptoClient *client);

#endif
