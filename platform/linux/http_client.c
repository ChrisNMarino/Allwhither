#include "http_client.h"

#include <libsoup/soup.h>
#include <math.h>
#include <string.h>

#define HTTP_DEFAULT_TIMEOUT_MS 30000

typedef struct _HttpRequest HttpRequest;

struct _HttpClient {
    JSGlobalContextRef context;
    JSObjectRef object;
    SoupSession *session;
    GList *requests;
    gboolean disposed;
    int inflight;
};

struct _HttpRequest {
    HttpClient *client;
    JSObjectRef resolve;
    JSObjectRef reject;
    SoupMessage *message;
    GCancellable *cancellable;
    guint timeout_id;
    gboolean started;
    gboolean settled;
    gboolean timed_out;
};

typedef struct {
    JSContextRef context;
    JSObjectRef headers;
} HeaderBuild;

static char *
js_copy(JSContextRef context, JSValueRef value)
{
    JSStringRef string = JSValueToStringCopy(context, value, NULL);
    if (string == NULL) {
        return g_strdup("");
    }
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

static void
set_utf8(JSContextRef context, JSObjectRef object, const char *name, const char *text)
{
    JSStringRef property = JSStringCreateWithUTF8CString(name);
    JSObjectSetProperty(context, object, property, js_utf8(context, text),
                        kJSPropertyAttributeNone, NULL);
    JSStringRelease(property);
}

static void
reject_with(HttpRequest *request, const char *message)
{
    if (request->settled || request->client->disposed) {
        return;
    }
    request->settled = TRUE;
    JSValueRef argument = js_utf8(request->client->context, message);
    JSValueRef exception = NULL;
    JSObjectCallAsFunction(request->client->context, request->reject, NULL, 1,
                           &argument, &exception);
    if (exception != NULL) {
        char *error = js_copy(request->client->context, exception);
        g_warning("JavaScript HTTP callback failed: %s", error);
        g_free(error);
    }
}

static void
add_response_header(const char *name, const char *value, gpointer data)
{
    HeaderBuild *build = data;
    char *lower = g_ascii_strdown(name, -1);
    JSStringRef property = JSStringCreateWithUTF8CString(lower);
    JSValueRef existing = JSObjectGetProperty(build->context, build->headers, property, NULL);
    char *combined = NULL;
    const char *stored = value;
    if (existing != NULL && !JSValueIsUndefined(build->context, existing)) {
        char *previous = js_copy(build->context, existing);
        combined = g_strdup_printf("%s, %s", previous, value);
        stored = combined;
        g_free(previous);
    }
    JSObjectSetProperty(build->context, build->headers, property,
                        js_utf8(build->context, stored),
                        kJSPropertyAttributeNone, NULL);
    JSStringRelease(property);
    g_free(combined);
    g_free(lower);
}

static void
resolve_with(HttpRequest *request, GBytes *body)
{
    if (request->settled || request->client->disposed) {
        return;
    }
    request->settled = TRUE;
    JSContextRef context = request->client->context;
    SoupMessage *message = request->message;
    unsigned status = soup_message_get_status(message);
    const char *reason = soup_message_get_reason_phrase(message);
    gsize body_size = 0;
    const char *body_bytes = body != NULL ? g_bytes_get_data(body, &body_size) : "";
    char *body_text = g_utf8_make_valid(body_bytes != NULL ? body_bytes : "", body_size);
    char *final_url = g_uri_to_string(soup_message_get_uri(message));
    JSObjectRef response = JSObjectMake(context, NULL, NULL);
    JSObjectRef headers = JSObjectMake(context, NULL, NULL);
    HeaderBuild build = { context, headers };
    soup_message_headers_foreach(soup_message_get_response_headers(message),
                                 add_response_header, &build);
    JSStringRef ok_name = JSStringCreateWithUTF8CString("ok");
    JSObjectSetProperty(context, response, ok_name,
                        JSValueMakeBoolean(context, status >= 200 && status <= 299),
                        kJSPropertyAttributeNone, NULL);
    JSStringRelease(ok_name);
    JSStringRef status_name = JSStringCreateWithUTF8CString("status");
    JSObjectSetProperty(context, response, status_name,
                        JSValueMakeNumber(context, status),
                        kJSPropertyAttributeNone, NULL);
    JSStringRelease(status_name);
    set_utf8(context, response, "statusText", reason != NULL ? reason : "");
    set_utf8(context, response, "body", body_text);
    set_utf8(context, response, "url", final_url != NULL ? final_url : "");
    JSStringRef headers_name = JSStringCreateWithUTF8CString("headers");
    JSObjectSetProperty(context, response, headers_name, headers,
                        kJSPropertyAttributeNone, NULL);
    JSStringRelease(headers_name);
    JSValueRef argument = response;
    JSValueRef exception = NULL;
    JSObjectCallAsFunction(context, request->resolve, NULL, 1, &argument, &exception);
    if (exception != NULL) {
        char *error = js_copy(context, exception);
        g_warning("JavaScript HTTP callback failed: %s", error);
        g_free(error);
    }
    g_free(body_text);
    g_free(final_url);
}

static void
finish_request(HttpRequest *request)
{
    HttpClient *client = request->client;
    if (request->timeout_id != 0) {
        g_source_remove(request->timeout_id);
        request->timeout_id = 0;
    }
    if (request->started) {
        client->requests = g_list_remove(client->requests, request);
        client->inflight -= 1;
        request->started = FALSE;
    }
    if (request->resolve != NULL) {
        JSValueUnprotect(client->context, request->resolve);
    }
    if (request->reject != NULL) {
        JSValueUnprotect(client->context, request->reject);
    }
    g_clear_object(&request->message);
    g_clear_object(&request->cancellable);
    g_free(request);
}

static gboolean
on_timeout(gpointer data)
{
    HttpRequest *request = data;
    request->timeout_id = 0;
    if (!request->settled) {
        request->timed_out = TRUE;
        g_cancellable_cancel(request->cancellable);
    }
    return G_SOURCE_REMOVE;
}

static void
on_response(GObject *source, GAsyncResult *result, gpointer data)
{
    HttpRequest *request = data;
    GError *error = NULL;
    GBytes *body = soup_session_send_and_read_finish(SOUP_SESSION(source), result, &error);
    if (request->timed_out) {
        reject_with(request, "Request timed out.");
    } else if (error != NULL) {
        reject_with(request, error->message);
    } else {
        resolve_with(request, body);
    }
    g_clear_error(&error);
    if (body != NULL) {
        g_bytes_unref(body);
    }
    finish_request(request);
}

static gboolean
valid_method(const char *method)
{
    if (method == NULL || !g_ascii_isalpha(method[0])) {
        return FALSE;
    }
    for (const char *cursor = method + 1; *cursor != '\0'; cursor++) {
        if (!g_ascii_isalnum(*cursor)) {
            return FALSE;
        }
    }
    return TRUE;
}

static JSObjectRef
make_promise(HttpClient *client, JSValueRef *exception, HttpRequest **request_out)
{
    JSObjectRef resolve = NULL;
    JSObjectRef reject = NULL;
    JSObjectRef promise = JSObjectMakeDeferredPromise(client->context, &resolve, &reject, exception);
    if (promise == NULL || resolve == NULL || reject == NULL) {
        return NULL;
    }
    HttpRequest *request = g_new0(HttpRequest, 1);
    request->client = client;
    request->resolve = resolve;
    request->reject = reject;
    request->cancellable = g_cancellable_new();
    JSValueProtect(client->context, resolve);
    JSValueProtect(client->context, reject);
    *request_out = request;
    return promise;
}

static gboolean
append_headers(JSContextRef context,
               JSObjectRef headers,
               SoupMessage *message,
               char **content_type,
               JSValueRef *exception)
{
    JSPropertyNameArrayRef names = JSObjectCopyPropertyNames(context, headers);
    size_t count = JSPropertyNameArrayGetCount(names);
    for (size_t index = 0; index < count; index++) {
        JSStringRef name_ref = JSPropertyNameArrayGetNameAtIndex(names, index);
        JSValueRef value = JSObjectGetProperty(context, headers, name_ref, exception);
        if (*exception != NULL) {
            JSPropertyNameArrayRelease(names);
            return FALSE;
        }
        if (JSValueIsObject(context, value) && !JSValueIsNull(context, value)) {
            JSPropertyNameArrayRelease(names);
            return FALSE;
        }
        size_t name_size = JSStringGetMaximumUTF8CStringSize(name_ref);
        char *name = g_malloc(name_size);
        JSStringGetUTF8CString(name_ref, name, name_size);
        char *text = js_copy(context, value);
        if (g_ascii_strcasecmp(name, "content-type") == 0) {
            g_free(*content_type);
            *content_type = g_strdup(text);
        } else if (name[0] != '\0') {
            soup_message_headers_append(soup_message_get_request_headers(message), name, text);
        }
        g_free(name);
        g_free(text);
    }
    JSPropertyNameArrayRelease(names);
    return TRUE;
}

static JSValueRef
start_request(JSContextRef context,
              JSObjectRef this_object,
              const char *method,
              const char *url,
              JSValueRef options_value,
              JSValueRef *exception)
{
    HttpClient *client = JSObjectGetPrivate(this_object);
    HttpRequest *request = NULL;
    JSObjectRef promise = make_promise(client, exception, &request);
    if (promise == NULL) {
        return JSValueMakeUndefined(context);
    }
    if (url == NULL || url[0] == '\0') {
        reject_with(request, "A URL is required.");
        finish_request(request);
        return promise;
    }
    if (!valid_method(method)) {
        reject_with(request, "Invalid HTTP method.");
        finish_request(request);
        return promise;
    }
    GError *uri_error = NULL;
    GUri *uri = g_uri_parse(url, G_URI_FLAGS_NONE, &uri_error);
    const char *scheme = uri != NULL ? g_uri_get_scheme(uri) : NULL;
    if (uri == NULL || scheme == NULL ||
        (g_ascii_strcasecmp(scheme, "http") != 0 &&
         g_ascii_strcasecmp(scheme, "https") != 0)) {
        reject_with(request, uri == NULL || scheme == NULL ? "Invalid URL." :
                    "Only http and https URLs are supported.");
        g_clear_error(&uri_error);
        if (uri != NULL) {
            g_uri_unref(uri);
        }
        finish_request(request);
        return promise;
    }
    g_uri_unref(uri);

    guint timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    char *body = NULL;
    char *content_type = NULL;
    SoupMessage *message = soup_message_new(method, url);
    if (message == NULL) {
        reject_with(request, "Invalid URL.");
        finish_request(request);
        return promise;
    }
    if (options_value != NULL &&
        !JSValueIsUndefined(context, options_value) &&
        !JSValueIsNull(context, options_value)) {
        if (!JSValueIsObject(context, options_value) ||
            JSValueIsArray(context, options_value)) {
            g_object_unref(message);
            reject_with(request, "HTTP options must be an object.");
            finish_request(request);
            return promise;
        }
        JSObjectRef options = JSValueToObject(context, options_value, exception);
        JSStringRef timeout_name = JSStringCreateWithUTF8CString("timeout");
        if (JSObjectHasProperty(context, options, timeout_name)) {
            JSValueRef timeout_value = JSObjectGetProperty(context, options, timeout_name, exception);
            double timeout = JSValueToNumber(context, timeout_value, exception);
            if (*exception != NULL || !isfinite(timeout) || timeout <= 0 || timeout > G_MAXUINT) {
                JSStringRelease(timeout_name);
                g_object_unref(message);
                reject_with(request, "HTTP timeout must be a positive number of milliseconds.");
                finish_request(request);
                return promise;
            }
            timeout_ms = (guint) timeout;
        }
        JSStringRelease(timeout_name);

        JSStringRef headers_name = JSStringCreateWithUTF8CString("headers");
        if (JSObjectHasProperty(context, options, headers_name)) {
            JSValueRef headers_value = JSObjectGetProperty(context, options, headers_name, exception);
            if (!JSValueIsObject(context, headers_value) || JSValueIsNull(context, headers_value) ||
                JSValueIsArray(context, headers_value)) {
                JSStringRelease(headers_name);
                g_object_unref(message);
                reject_with(request, "HTTP headers must be an object.");
                finish_request(request);
                return promise;
            }
            JSObjectRef headers = JSValueToObject(context, headers_value, exception);
            if (!append_headers(context, headers, message, &content_type, exception)) {
                JSStringRelease(headers_name);
                g_object_unref(message);
                g_free(content_type);
                reject_with(request, "HTTP headers must be an object.");
                finish_request(request);
                return promise;
            }
        }
        JSStringRelease(headers_name);

        JSStringRef body_name = JSStringCreateWithUTF8CString("body");
        if (JSObjectHasProperty(context, options, body_name)) {
            JSValueRef body_value = JSObjectGetProperty(context, options, body_name, exception);
            if (!JSValueIsUndefined(context, body_value) && !JSValueIsNull(context, body_value)) {
                if (!JSValueIsString(context, body_value)) {
                    JSStringRelease(body_name);
                    g_object_unref(message);
                    g_free(content_type);
                    reject_with(request, "HTTP body must be a string.");
                    finish_request(request);
                    return promise;
                }
                body = js_copy(context, body_value);
            }
        }
        JSStringRelease(body_name);
    }

    if (body != NULL &&
        g_ascii_strcasecmp(method, "GET") != 0 &&
        g_ascii_strcasecmp(method, "HEAD") != 0) {
        GBytes *bytes = g_bytes_new_take(body, strlen(body));
        soup_message_set_request_body_from_bytes(
            message,
            content_type != NULL ? content_type : "text/plain;charset=UTF-8",
            bytes);
        g_bytes_unref(bytes);
        body = NULL;
    }
    g_free(body);
    g_free(content_type);

    request->message = message;
    request->timeout_id = g_timeout_add(timeout_ms, on_timeout, request);
    request->started = TRUE;
    client->requests = g_list_prepend(client->requests, request);
    client->inflight += 1;
    soup_session_send_and_read_async(client->session, message, G_PRIORITY_DEFAULT,
                                     request->cancellable, on_response, request);
    return promise;
}

static JSValueRef
call_named(JSContextRef context,
           JSObjectRef function,
           JSObjectRef this_object,
           size_t argument_count,
           const JSValueRef arguments[],
           JSValueRef *exception,
           const char *method)
{
    (void) function;
    if (argument_count < 1 || !JSValueIsString(context, arguments[0])) {
        HttpRequest *request = NULL;
        JSObjectRef promise = make_promise(JSObjectGetPrivate(this_object), exception, &request);
        if (promise == NULL) {
            return JSValueMakeUndefined(context);
        }
        reject_with(request, "A URL is required.");
        finish_request(request);
        return promise;
    }
    char *url = js_copy(context, arguments[0]);
    JSValueRef options = argument_count >= 2 ? arguments[1] : NULL;
    JSValueRef promise = start_request(context, this_object, method, url, options, exception);
    g_free(url);
    return promise;
}

#define HTTP_METHOD(name, verb) \
    static JSValueRef http_##name(JSContextRef context, JSObjectRef function, \
                                  JSObjectRef this_object, size_t argument_count, \
                                  const JSValueRef arguments[], JSValueRef *exception) \
    { \
        return call_named(context, function, this_object, argument_count, arguments, \
                          exception, verb); \
    }

HTTP_METHOD(get, "GET")
HTTP_METHOD(post, "POST")
HTTP_METHOD(put, "PUT")
HTTP_METHOD(patch, "PATCH")
HTTP_METHOD(delete, "DELETE")
HTTP_METHOD(head, "HEAD")
HTTP_METHOD(options, "OPTIONS")

static JSValueRef
http_request(JSContextRef context,
             JSObjectRef function,
             JSObjectRef this_object,
             size_t argument_count,
             const JSValueRef arguments[],
             JSValueRef *exception)
{
    (void) function;
    HttpClient *client = JSObjectGetPrivate(this_object);
    if (argument_count < 2 ||
        !JSValueIsString(context, arguments[0]) ||
        !JSValueIsString(context, arguments[1])) {
        HttpRequest *request = NULL;
        JSObjectRef promise = make_promise(client, exception, &request);
        if (promise == NULL) {
            return JSValueMakeUndefined(context);
        }
        reject_with(request, "A method and URL are required.");
        finish_request(request);
        return promise;
    }
    char *method = js_copy(context, arguments[0]);
    char *url = js_copy(context, arguments[1]);
    char *normalized = g_ascii_strup(method, -1);
    JSValueRef options = argument_count >= 3 ? arguments[2] : NULL;
    JSValueRef promise = start_request(context, this_object, normalized, url, options, exception);
    g_free(normalized);
    g_free(method);
    g_free(url);
    return promise;
}

static JSStaticFunction http_functions[] = {
    { "get", http_get, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "post", http_post, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "put", http_put, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "patch", http_patch, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "delete", http_delete, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "head", http_head, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "options", http_options, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "request", http_request, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { NULL, NULL, 0 }
};

HttpClient *
http_client_new(JSGlobalContextRef context)
{
    HttpClient *client = g_new0(HttpClient, 1);
    client->context = context;
    client->session = soup_session_new();
    JSClassDefinition definition = kJSClassDefinitionEmpty;
    definition.className = "AllwhitherHttpClient";
    definition.staticFunctions = http_functions;
    JSClassRef klass = JSClassCreate(&definition);
    client->object = JSObjectMake(context, klass, client);
    JSClassRelease(klass);
    JSValueProtect(context, client->object);
    return client;
}

JSObjectRef
http_client_js_object(HttpClient *client)
{
    return client->object;
}

void
http_client_free(HttpClient *client)
{
    if (client == NULL) {
        return;
    }
    client->disposed = TRUE;
    for (GList *item = client->requests; item != NULL; item = item->next) {
        HttpRequest *request = item->data;
        if (request->timeout_id != 0) {
            g_source_remove(request->timeout_id);
            request->timeout_id = 0;
        }
        g_cancellable_cancel(request->cancellable);
    }
    while (client->inflight > 0) {
        g_main_context_iteration(NULL, TRUE);
    }
    JSValueUnprotect(client->context, client->object);
    g_clear_object(&client->session);
    g_free(client);
}
