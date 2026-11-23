#ifndef OMNI_HTTP_CLIENT_H
#define OMNI_HTTP_CLIENT_H

#include <JavaScriptCore/JavaScript.h>

typedef struct _HttpClient HttpClient;

HttpClient *http_client_new(JSGlobalContextRef context);
JSObjectRef http_client_js_object(HttpClient *client);
void http_client_free(HttpClient *client);

#endif
