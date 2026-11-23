#include "crypto_client.h"

#include <json-glib/json-glib.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <string.h>

struct _CryptoClient {
    JSGlobalContextRef context;
    JSObjectRef object;
};

typedef struct {
    CryptoClient *client;
    JSObjectRef resolve;
    JSObjectRef reject;
} CryptoRequest;

static char *
js_copy_string(JSContextRef context, JSValueRef value)
{
    if (value == NULL || JSValueIsNull(context, value) || JSValueIsUndefined(context, value)) {
        return g_strdup("");
    }
    JSStringRef string = JSValueToStringCopy(context, value, NULL);
    if (string == NULL) return g_strdup("");
    size_t size = JSStringGetMaximumUTF8CStringSize(string);
    char *result = g_malloc(size);
    JSStringGetUTF8CString(string, result, size);
    JSStringRelease(string);
    return result;
}

static JSValueRef
js_utf8(JSContextRef context, const char *text)
{
    JSStringRef string = JSStringCreateWithUTF8CString(text != NULL ? text : "");
    JSValueRef value = JSValueMakeString(context, string);
    JSStringRelease(string);
    return value;
}

static JSObjectRef
make_promise(CryptoClient *client, JSValueRef *exception, CryptoRequest **request_out)
{
    JSObjectRef resolve = NULL;
    JSObjectRef reject = NULL;
    JSObjectRef promise = JSObjectMakeDeferredPromise(client->context, &resolve, &reject, exception);
    if (promise == NULL || resolve == NULL || reject == NULL) {
        return NULL;
    }
    CryptoRequest *req = g_new0(CryptoRequest, 1);
    req->client = client;
    req->resolve = resolve;
    req->reject = reject;
    JSValueProtect(client->context, resolve);
    JSValueProtect(client->context, reject);
    *request_out = req;
    return promise;
}

static void
finish_request(CryptoRequest *req)
{
    if (req == NULL) return;
    if (req->resolve != NULL) {
        JSValueUnprotect(req->client->context, req->resolve);
    }
    if (req->reject != NULL) {
        JSValueUnprotect(req->client->context, req->reject);
    }
    g_free(req);
}

static void
reject_with(CryptoRequest *req, const char *message)
{
    JSValueRef arg = js_utf8(req->client->context, message != NULL ? message : "Crypto operation failed.");
    JSValueRef exception = NULL;
    JSObjectCallAsFunction(req->client->context, req->reject, NULL, 1, &arg, &exception);
    finish_request(req);
}

static void
resolve_with(CryptoRequest *req, JSValueRef result)
{
    JSValueRef exception = NULL;
    JSObjectCallAsFunction(req->client->context, req->resolve, NULL, 1, &result, &exception);
    finish_request(req);
}

static JSValueRef
crypto_encrypt(JSContextRef context, JSObjectRef function, JSObjectRef this_object,
               size_t argument_count, const JSValueRef arguments[], JSValueRef *exception)
{
    (void) function;
    CryptoClient *client = JSObjectGetPrivate(this_object);
    CryptoRequest *req = NULL;
    JSObjectRef promise = make_promise(client, exception, &req);
    if (promise == NULL) return JSValueMakeUndefined(context);

    if (argument_count < 2) {
        reject_with(req, "Plaintext and password must be strings.");
        return promise;
    }

    char *plaintext = js_copy_string(context, arguments[0]);
    char *password = js_copy_string(context, arguments[1]);

    if (password[0] == '\0') {
        g_free(plaintext);
        g_free(password);
        reject_with(req, "Password must not be empty.");
        return promise;
    }

    unsigned char salt[16];
    unsigned char iv[12];
    unsigned char key[32];
    unsigned char tag[16];

    if (RAND_bytes(salt, sizeof(salt)) <= 0 || RAND_bytes(iv, sizeof(iv)) <= 0) {
        g_free(plaintext);
        g_free(password);
        reject_with(req, "Failed to generate random salt/iv.");
        return promise;
    }

    if (PKCS5_PBKDF2_HMAC(password, strlen(password), salt, sizeof(salt), 10000, EVP_sha256(), 32, key) != 1) {
        g_free(plaintext);
        g_free(password);
        reject_with(req, "Key derivation failed.");
        return promise;
    }
    g_free(password);

    int plain_len = strlen(plaintext);
    unsigned char *ciphertext = g_malloc(plain_len + 16);
    int len = 0, cipher_len = 0;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL);
    EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv);
    EVP_EncryptUpdate(ctx, ciphertext, &len, (const unsigned char *)plaintext, plain_len);
    cipher_len = len;
    EVP_EncryptFinal_ex(ctx, ciphertext + len, &len);
    cipher_len += len;
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag);
    EVP_CIPHER_CTX_free(ctx);
    g_free(plaintext);

    char *b64_salt = g_base64_encode(salt, sizeof(salt));
    char *b64_iv = g_base64_encode(iv, sizeof(iv));
    char *b64_tag = g_base64_encode(tag, sizeof(tag));
    char *b64_data = g_base64_encode(ciphertext, cipher_len);
    g_free(ciphertext);

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "version");
    json_builder_add_int_value(builder, 1);
    json_builder_set_member_name(builder, "salt");
    json_builder_add_string_value(builder, b64_salt);
    json_builder_set_member_name(builder, "iv");
    json_builder_add_string_value(builder, b64_iv);
    json_builder_set_member_name(builder, "tag");
    json_builder_add_string_value(builder, b64_tag);
    json_builder_set_member_name(builder, "data");
    json_builder_add_string_value(builder, b64_data);
    json_builder_end_object(builder);

    g_free(b64_salt);
    g_free(b64_iv);
    g_free(b64_tag);
    g_free(b64_data);

    JsonGenerator *generator = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(generator, root);
    char *json_output = json_generator_to_data(generator, NULL);

    json_node_free(root);
    g_object_unref(builder);
    g_object_unref(generator);

    JSValueRef res = js_utf8(context, json_output);
    g_free(json_output);

    resolve_with(req, res);
    return promise;
}

static JSValueRef
crypto_decrypt(JSContextRef context, JSObjectRef function, JSObjectRef this_object,
               size_t argument_count, const JSValueRef arguments[], JSValueRef *exception)
{
    (void) function;
    CryptoClient *client = JSObjectGetPrivate(this_object);
    CryptoRequest *req = NULL;
    JSObjectRef promise = make_promise(client, exception, &req);
    if (promise == NULL) return JSValueMakeUndefined(context);

    if (argument_count < 2) {
        reject_with(req, "Ciphertext and password must be strings.");
        return promise;
    }

    char *ciphertext_json = js_copy_string(context, arguments[0]);
    char *password = js_copy_string(context, arguments[1]);

    if (password[0] == '\0') {
        g_free(ciphertext_json);
        g_free(password);
        reject_with(req, "Password must not be empty.");
        return promise;
    }

    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    if (!json_parser_load_from_data(parser, ciphertext_json, -1, &error)) {
        g_free(ciphertext_json);
        g_free(password);
        g_clear_error(&error);
        g_object_unref(parser);
        reject_with(req, "Invalid encrypted data format.");
        return promise;
    }
    g_free(ciphertext_json);

    JsonNode *root_node = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root_node)) {
        g_free(password);
        g_object_unref(parser);
        reject_with(req, "Malformed encrypted payload.");
        return promise;
    }
    JsonObject *root = json_node_get_object(root_node);

    if (!json_object_has_member(root, "salt") ||
        !json_object_has_member(root, "iv") ||
        !json_object_has_member(root, "tag") ||
        !json_object_has_member(root, "data")) {
        g_free(password);
        g_object_unref(parser);
        reject_with(req, "Malformed encrypted payload.");
        return promise;
    }

    const char *b64_salt = json_object_get_string_member(root, "salt");
    const char *b64_iv = json_object_get_string_member(root, "iv");
    const char *b64_tag = json_object_get_string_member(root, "tag");
    const char *b64_data = json_object_get_string_member(root, "data");

    gsize salt_len = 0, iv_len = 0, tag_len = 0, data_len = 0;
    guchar *salt = g_base64_decode(b64_salt, &salt_len);
    guchar *iv = g_base64_decode(b64_iv, &iv_len);
    guchar *tag = g_base64_decode(b64_tag, &tag_len);
    guchar *data = g_base64_decode(b64_data, &data_len);

    g_object_unref(parser);

    if (salt_len != 16 || iv_len != 12 || tag_len != 16 || data == NULL) {
        g_free(password);
        g_free(salt);
        g_free(iv);
        g_free(tag);
        g_free(data);
        reject_with(req, "Malformed encrypted payload.");
        return promise;
    }

    unsigned char key[32];
    if (PKCS5_PBKDF2_HMAC(password, strlen(password), salt, salt_len, 10000, EVP_sha256(), 32, key) != 1) {
        g_free(password);
        g_free(salt);
        g_free(iv);
        g_free(tag);
        g_free(data);
        reject_with(req, "Key derivation failed.");
        return promise;
    }
    g_free(password);
    g_free(salt);

    unsigned char *decrypted = g_malloc(data_len + 1);
    int len = 0, dec_len = 0;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL);
    EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv);
    EVP_DecryptUpdate(ctx, decrypted, &len, data, (int)data_len);
    dec_len = len;
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, tag);
    int ret = EVP_DecryptFinal_ex(ctx, decrypted + len, &len);
    dec_len += len;
    EVP_CIPHER_CTX_free(ctx);

    g_free(iv);
    g_free(tag);
    g_free(data);

    if (ret <= 0) {
        g_free(decrypted);
        reject_with(req, "Decryption failed: invalid password or corrupted data.");
        return promise;
    }

    decrypted[dec_len] = '\0';
    JSValueRef res = js_utf8(context, (const char *)decrypted);
    g_free(decrypted);

    resolve_with(req, res);
    return promise;
}

static JSStaticFunction crypto_functions[] = {
    { "encrypt", crypto_encrypt, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "decrypt", crypto_decrypt, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { NULL, NULL, 0 }
};

CryptoClient *
crypto_client_new(JSGlobalContextRef context)
{
    CryptoClient *client = g_new0(CryptoClient, 1);
    client->context = context;

    JSClassDefinition crypto_def = kJSClassDefinitionEmpty;
    crypto_def.className = "OmniCrypto";
    crypto_def.staticFunctions = crypto_functions;
    JSClassRef crypto_class = JSClassCreate(&crypto_def);
    client->object = JSObjectMake(context, crypto_class, client);
    JSClassRelease(crypto_class);

    return client;
}

JSObjectRef
crypto_client_js_object(CryptoClient *client)
{
    return client != NULL ? client->object : NULL;
}

void
crypto_client_free(CryptoClient *client)
{
    if (client == NULL) return;
    g_free(client);
}
