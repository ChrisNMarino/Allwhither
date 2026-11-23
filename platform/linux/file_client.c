#include "file_client.h"

#include <gio/gio.h>
#include <string.h>

typedef struct _FileClientRequest FileClientRequest;

struct _FileClient {
    JSGlobalContextRef context;
    JSObjectRef object;
    JSObjectRef storage_object;
    GtkWindow *window;
    char *project_name;
    char *storage_root;
};

struct _FileClientRequest {
    FileClient *client;
    JSObjectRef resolve;
    JSObjectRef reject;
    char *content;
    char *target_path;
};

static char *
js_copy_string(JSContextRef context, JSValueRef value)
{
    if (value == NULL || JSValueIsNull(context, value) || JSValueIsUndefined(context, value)) {
        return g_strdup("");
    }
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
set_js_property(JSContextRef context, JSObjectRef object, const char *name, JSValueRef value)
{
    JSStringRef property = JSStringCreateWithUTF8CString(name);
    JSObjectSetProperty(context, object, property, value, kJSPropertyAttributeNone, NULL);
    JSStringRelease(property);
}

static JSObjectRef
make_promise(FileClient *client, JSValueRef *exception, FileClientRequest **request_out)
{
    JSObjectRef resolve = NULL;
    JSObjectRef reject = NULL;
    JSObjectRef promise = JSObjectMakeDeferredPromise(client->context, &resolve, &reject, exception);
    if (promise == NULL || resolve == NULL || reject == NULL) {
        return NULL;
    }
    FileClientRequest *req = g_new0(FileClientRequest, 1);
    req->client = client;
    req->resolve = resolve;
    req->reject = reject;
    JSValueProtect(client->context, resolve);
    JSValueProtect(client->context, reject);
    *request_out = req;
    return promise;
}

static void
finish_request(FileClientRequest *req)
{
    if (req == NULL) return;
    if (req->resolve != NULL) {
        JSValueUnprotect(req->client->context, req->resolve);
    }
    if (req->reject != NULL) {
        JSValueUnprotect(req->client->context, req->reject);
    }
    g_free(req->content);
    g_free(req->target_path);
    g_free(req);
}

static void
reject_with(FileClientRequest *req, const char *message)
{
    JSValueRef arg = js_utf8(req->client->context, message != NULL ? message : "File operation failed.");
    JSValueRef exception = NULL;
    JSObjectCallAsFunction(req->client->context, req->reject, NULL, 1, &arg, &exception);
    finish_request(req);
}

static void
resolve_with(FileClientRequest *req, JSValueRef result)
{
    JSValueRef exception = NULL;
    JSObjectCallAsFunction(req->client->context, req->resolve, NULL, 1, &result, &exception);
    finish_request(req);
}

static void
resolve_cancel(FileClientRequest *req)
{
    JSContextRef context = req->client->context;
    JSObjectRef obj = JSObjectMake(context, NULL, NULL);
    set_js_property(context, obj, "ok", JSValueMakeBoolean(context, false));
    set_js_property(context, obj, "canceled", JSValueMakeBoolean(context, true));
    resolve_with(req, obj);
}

static char *
resolve_storage_path(FileClient *client, const char *relative_path, GError **error)
{
    if (relative_path == NULL || *relative_path == '\0') {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "A valid file path is required.");
        return NULL;
    }
    while (*relative_path == '/' || *relative_path == '\\') {
        relative_path++;
    }
    if (strstr(relative_path, "..") != NULL) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "Path traversal is not allowed.");
        return NULL;
    }
    return g_build_filename(client->storage_root, relative_path, NULL);
}

static void
apply_dialog_filters(GtkFileDialog *dialog, JSContextRef context, JSObjectRef options)
{
    JSStringRef filters_prop = JSStringCreateWithUTF8CString("filters");
    if (!JSObjectHasProperty(context, options, filters_prop)) {
        JSStringRelease(filters_prop);
        return;
    }
    JSValueRef filters_val = JSObjectGetProperty(context, options, filters_prop, NULL);
    JSStringRelease(filters_prop);
    if (!JSValueIsObject(context, filters_val)) return;

    JSObjectRef filters_array = JSValueToObject(context, filters_val, NULL);
    JSStringRef length_prop = JSStringCreateWithUTF8CString("length");
    JSValueRef length_val = JSObjectGetProperty(context, filters_array, length_prop, NULL);
    JSStringRelease(length_prop);
    if (!JSValueIsNumber(context, length_val)) return;

    size_t count = (size_t) JSValueToNumber(context, length_val, NULL);
    if (count == 0) return;

    GListStore *store = g_list_store_new(GTK_TYPE_FILE_FILTER);
    for (size_t i = 0; i < count; i++) {
        JSValueRef item_val = JSObjectGetPropertyAtIndex(context, filters_array, i, NULL);
        if (!JSValueIsObject(context, item_val)) continue;
        JSObjectRef item = JSValueToObject(context, item_val, NULL);

        JSStringRef name_prop = JSStringCreateWithUTF8CString("name");
        JSValueRef name_val = JSObjectGetProperty(context, item, name_prop, NULL);
        JSStringRelease(name_prop);
        char *name = js_copy_string(context, name_val);

        GtkFileFilter *filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, name[0] ? name : "Files");
        g_free(name);

        JSStringRef exts_prop = JSStringCreateWithUTF8CString("extensions");
        if (JSObjectHasProperty(context, item, exts_prop)) {
            JSValueRef exts_val = JSObjectGetProperty(context, item, exts_prop, NULL);
            if (JSValueIsObject(context, exts_val)) {
                JSObjectRef exts_arr = JSValueToObject(context, exts_val, NULL);
                JSStringRef exts_len_prop = JSStringCreateWithUTF8CString("length");
                JSValueRef exts_len_val = JSObjectGetProperty(context, exts_arr, exts_len_prop, NULL);
                JSStringRelease(exts_len_prop);
                size_t ext_count = (size_t) JSValueToNumber(context, exts_len_val, NULL);
                for (size_t j = 0; j < ext_count; j++) {
                    JSValueRef ext_val = JSObjectGetPropertyAtIndex(context, exts_arr, j, NULL);
                    char *ext = js_copy_string(context, ext_val);
                    const char *clean_ext = ext;
                    if (clean_ext[0] == '.') clean_ext++;
                    if (clean_ext[0] == '*') clean_ext++;
                    if (clean_ext[0] == '.') clean_ext++;
                    if (clean_ext[0]) {
                        gtk_file_filter_add_suffix(filter, clean_ext);
                    }
                    g_free(ext);
                }
            }
        }
        JSStringRelease(exts_prop);
        g_list_store_append(store, filter);
        g_object_unref(filter);
    }
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(store));
    g_object_unref(store);
}

static void
on_open_finish(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    FileClientRequest *req = user_data;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source_object);
    GError *error = NULL;
    GFile *file = gtk_file_dialog_open_finish(dialog, result, &error);
    if (file == NULL) {
        if (error != NULL && g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)) {
            g_clear_error(&error);
            resolve_cancel(req);
            return;
        }
        char *msg = error ? g_strdup(error->message) : g_strdup("Open failed.");
        g_clear_error(&error);
        reject_with(req, msg);
        g_free(msg);
        return;
    }

    char *contents = NULL;
    gsize length = 0;
    if (!g_file_load_contents(file, NULL, &contents, &length, NULL, &error)) {
        char *msg = error ? g_strdup(error->message) : g_strdup("Failed to read file.");
        g_clear_error(&error);
        g_object_unref(file);
        reject_with(req, msg);
        g_free(msg);
        return;
    }

    char *basename = g_file_get_basename(file);
    char *path = g_file_get_path(file);
    g_object_unref(file);

    JSContextRef context = req->client->context;
    JSObjectRef res = JSObjectMake(context, NULL, NULL);
    set_js_property(context, res, "ok", JSValueMakeBoolean(context, true));
    set_js_property(context, res, "canceled", JSValueMakeBoolean(context, false));
    set_js_property(context, res, "name", js_utf8(context, basename));
    set_js_property(context, res, "content", js_utf8(context, contents));
    set_js_property(context, res, "path", path ? js_utf8(context, path) : JSValueMakeNull(context));

    g_free(contents);
    g_free(basename);
    g_free(path);

    resolve_with(req, res);
}

static void
on_save_finish(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    FileClientRequest *req = user_data;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source_object);
    GError *error = NULL;
    GFile *file = gtk_file_dialog_save_finish(dialog, result, &error);
    if (file == NULL) {
        if (error != NULL && g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)) {
            g_clear_error(&error);
            resolve_cancel(req);
            return;
        }
        char *msg = error ? g_strdup(error->message) : g_strdup("Save failed.");
        g_clear_error(&error);
        reject_with(req, msg);
        g_free(msg);
        return;
    }

    const char *content_to_write = req->content ? req->content : "";
    if (!g_file_replace_contents(file, content_to_write, strlen(content_to_write),
                                 NULL, FALSE, G_FILE_CREATE_NONE, NULL, NULL, &error)) {
        char *msg = error ? g_strdup(error->message) : g_strdup("Failed to write file.");
        g_clear_error(&error);
        g_object_unref(file);
        reject_with(req, msg);
        g_free(msg);
        return;
    }

    char *basename = g_file_get_basename(file);
    char *path = g_file_get_path(file);
    g_object_unref(file);

    JSContextRef context = req->client->context;
    JSObjectRef res = JSObjectMake(context, NULL, NULL);
    set_js_property(context, res, "ok", JSValueMakeBoolean(context, true));
    set_js_property(context, res, "canceled", JSValueMakeBoolean(context, false));
    set_js_property(context, res, "name", js_utf8(context, basename));
    set_js_property(context, res, "path", path ? js_utf8(context, path) : JSValueMakeNull(context));

    g_free(basename);
    g_free(path);

    resolve_with(req, res);
}

static JSValueRef
file_open(JSContextRef context, JSObjectRef function, JSObjectRef this_object,
          size_t argument_count, const JSValueRef arguments[], JSValueRef *exception)
{
    (void) function;
    FileClient *client = JSObjectGetPrivate(this_object);
    FileClientRequest *req = NULL;
    JSObjectRef promise = make_promise(client, exception, &req);
    if (promise == NULL) return JSValueMakeUndefined(context);

    GtkFileDialog *dialog = gtk_file_dialog_new();
    if (argument_count > 0 && JSValueIsObject(context, arguments[0])) {
        JSObjectRef options = JSValueToObject(context, arguments[0], NULL);
        JSStringRef title_prop = JSStringCreateWithUTF8CString("title");
        if (JSObjectHasProperty(context, options, title_prop)) {
            JSValueRef title_val = JSObjectGetProperty(context, options, title_prop, NULL);
            char *title = js_copy_string(context, title_val);
            if (title[0]) gtk_file_dialog_set_title(dialog, title);
            g_free(title);
        }
        JSStringRelease(title_prop);
        apply_dialog_filters(dialog, context, options);
    }

    gtk_file_dialog_open(dialog, client->window, NULL, on_open_finish, req);
    g_object_unref(dialog);
    return promise;
}

static JSValueRef
file_save_internal(JSContextRef context, FileClient *client, size_t argument_count,
                   const JSValueRef arguments[], JSValueRef *exception, gboolean force_save_as)
{
    FileClientRequest *req = NULL;
    JSObjectRef promise = make_promise(client, exception, &req);
    if (promise == NULL) return JSValueMakeUndefined(context);

    char *suggested_name = g_strdup("untitled.txt");
    char *content = NULL;
    char *target_path = NULL;

    if (argument_count > 0 && JSValueIsObject(context, arguments[0])) {
        JSObjectRef options = JSValueToObject(context, arguments[0], NULL);
        JSStringRef content_prop = JSStringCreateWithUTF8CString("content");
        if (JSObjectHasProperty(context, options, content_prop)) {
            content = js_copy_string(context, JSObjectGetProperty(context, options, content_prop, NULL));
        }
        JSStringRelease(content_prop);

        JSStringRef name_prop = JSStringCreateWithUTF8CString("name");
        if (JSObjectHasProperty(context, options, name_prop)) {
            char *val = js_copy_string(context, JSObjectGetProperty(context, options, name_prop, NULL));
            if (val[0]) {
                g_free(suggested_name);
                suggested_name = val;
            } else {
                g_free(val);
            }
        }
        JSStringRelease(name_prop);

        JSStringRef path_prop = JSStringCreateWithUTF8CString("path");
        if (JSObjectHasProperty(context, options, path_prop)) {
            char *val = js_copy_string(context, JSObjectGetProperty(context, options, path_prop, NULL));
            if (val[0]) target_path = val; else g_free(val);
        }
        JSStringRelease(path_prop);
    }

    req->content = content ? content : g_strdup("");

    if (!force_save_as && target_path != NULL) {
        GError *error = NULL;
        if (!g_file_set_contents(target_path, req->content, strlen(req->content), &error)) {
            char *msg = error ? g_strdup(error->message) : g_strdup("Failed to write file.");
            g_clear_error(&error);
            g_free(target_path);
            g_free(suggested_name);
            reject_with(req, msg);
            g_free(msg);
            return promise;
        }
        char *basename = g_path_get_basename(target_path);
        JSObjectRef res = JSObjectMake(context, NULL, NULL);
        set_js_property(context, res, "ok", JSValueMakeBoolean(context, true));
        set_js_property(context, res, "canceled", JSValueMakeBoolean(context, false));
        set_js_property(context, res, "name", js_utf8(context, basename));
        set_js_property(context, res, "path", js_utf8(context, target_path));
        g_free(basename);
        g_free(target_path);
        g_free(suggested_name);
        resolve_with(req, res);
        return promise;
    }
    g_free(target_path);

    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_initial_name(dialog, suggested_name);
    g_free(suggested_name);

    if (argument_count > 0 && JSValueIsObject(context, arguments[0])) {
        JSObjectRef options = JSValueToObject(context, arguments[0], NULL);
        JSStringRef title_prop = JSStringCreateWithUTF8CString("title");
        if (JSObjectHasProperty(context, options, title_prop)) {
            char *title = js_copy_string(context, JSObjectGetProperty(context, options, title_prop, NULL));
            if (title[0]) gtk_file_dialog_set_title(dialog, title);
            g_free(title);
        }
        JSStringRelease(title_prop);
        apply_dialog_filters(dialog, context, options);
    }

    gtk_file_dialog_save(dialog, client->window, NULL, on_save_finish, req);
    g_object_unref(dialog);
    return promise;
}

static JSValueRef
file_save(JSContextRef context, JSObjectRef function, JSObjectRef this_object,
          size_t argument_count, const JSValueRef arguments[], JSValueRef *exception)
{
    (void) function;
    FileClient *client = JSObjectGetPrivate(this_object);
    return file_save_internal(context, client, argument_count, arguments, exception, FALSE);
}

static JSValueRef
file_save_as(JSContextRef context, JSObjectRef function, JSObjectRef this_object,
             size_t argument_count, const JSValueRef arguments[], JSValueRef *exception)
{
    (void) function;
    FileClient *client = JSObjectGetPrivate(this_object);
    return file_save_internal(context, client, argument_count, arguments, exception, TRUE);
}

static JSValueRef
storage_read(JSContextRef context, JSObjectRef function, JSObjectRef this_object,
             size_t argument_count, const JSValueRef arguments[], JSValueRef *exception)
{
    (void) function;
    FileClient *client = JSObjectGetPrivate(this_object);
    FileClientRequest *req = NULL;
    JSObjectRef promise = make_promise(client, exception, &req);
    if (promise == NULL) return JSValueMakeUndefined(context);

    if (argument_count < 1) {
        reject_with(req, "A file path is required.");
        return promise;
    }
    char *rel_path = js_copy_string(context, arguments[0]);
    GError *error = NULL;
    char *full_path = resolve_storage_path(client, rel_path, &error);
    g_free(rel_path);
    if (full_path == NULL) {
        char *msg = error ? g_strdup(error->message) : g_strdup("Invalid path.");
        g_clear_error(&error);
        reject_with(req, msg);
        g_free(msg);
        return promise;
    }

    char *contents = NULL;
    gsize length = 0;
    if (!g_file_get_contents(full_path, &contents, &length, &error)) {
        char *msg = error ? g_strdup(error->message) : g_strdup("File not found.");
        g_clear_error(&error);
        g_free(full_path);
        reject_with(req, msg);
        g_free(msg);
        return promise;
    }
    g_free(full_path);

    JSValueRef res = js_utf8(context, contents);
    g_free(contents);
    resolve_with(req, res);
    return promise;
}

static JSValueRef
storage_write(JSContextRef context, JSObjectRef function, JSObjectRef this_object,
              size_t argument_count, const JSValueRef arguments[], JSValueRef *exception)
{
    (void) function;
    FileClient *client = JSObjectGetPrivate(this_object);
    FileClientRequest *req = NULL;
    JSObjectRef promise = make_promise(client, exception, &req);
    if (promise == NULL) return JSValueMakeUndefined(context);

    if (argument_count < 1) {
        reject_with(req, "A file path is required.");
        return promise;
    }
    char *rel_path = js_copy_string(context, arguments[0]);
    char *content = argument_count >= 2 ? js_copy_string(context, arguments[1]) : g_strdup("");

    GError *error = NULL;
    char *full_path = resolve_storage_path(client, rel_path, &error);
    if (full_path == NULL) {
        char *msg = error ? g_strdup(error->message) : g_strdup("Invalid path.");
        g_clear_error(&error);
        g_free(rel_path);
        g_free(content);
        reject_with(req, msg);
        g_free(msg);
        return promise;
    }

    char *dir = g_path_get_dirname(full_path);
    if (g_mkdir_with_parents(dir, 0755) != 0) {
        g_free(dir);
        g_free(full_path);
        g_free(rel_path);
        g_free(content);
        reject_with(req, "Could not create directory.");
        return promise;
    }
    g_free(dir);

    if (!g_file_set_contents(full_path, content, strlen(content), &error)) {
        char *msg = error ? g_strdup(error->message) : g_strdup("Failed to write file.");
        g_clear_error(&error);
        g_free(full_path);
        g_free(rel_path);
        g_free(content);
        reject_with(req, msg);
        g_free(msg);
        return promise;
    }
    g_free(full_path);
    g_free(content);

    JSObjectRef res = JSObjectMake(context, NULL, NULL);
    set_js_property(context, res, "ok", JSValueMakeBoolean(context, true));
    set_js_property(context, res, "path", js_utf8(context, rel_path));
    g_free(rel_path);

    resolve_with(req, res);
    return promise;
}

static JSValueRef
storage_delete(JSContextRef context, JSObjectRef function, JSObjectRef this_object,
               size_t argument_count, const JSValueRef arguments[], JSValueRef *exception)
{
    (void) function;
    FileClient *client = JSObjectGetPrivate(this_object);
    FileClientRequest *req = NULL;
    JSObjectRef promise = make_promise(client, exception, &req);
    if (promise == NULL) return JSValueMakeUndefined(context);

    if (argument_count < 1) {
        reject_with(req, "A file path is required.");
        return promise;
    }
    char *rel_path = js_copy_string(context, arguments[0]);
    GError *error = NULL;
    char *full_path = resolve_storage_path(client, rel_path, &error);
    g_free(rel_path);
    if (full_path == NULL) {
        char *msg = error ? g_strdup(error->message) : g_strdup("Invalid path.");
        g_clear_error(&error);
        reject_with(req, msg);
        g_free(msg);
        return promise;
    }

    GFile *target = g_file_new_for_path(full_path);
    g_file_trash(target, NULL, NULL);
    g_file_delete(target, NULL, NULL);
    g_object_unref(target);
    g_free(full_path);

    JSObjectRef res = JSObjectMake(context, NULL, NULL);
    set_js_property(context, res, "ok", JSValueMakeBoolean(context, true));
    resolve_with(req, res);
    return promise;
}

static JSValueRef
storage_exists(JSContextRef context, JSObjectRef function, JSObjectRef this_object,
               size_t argument_count, const JSValueRef arguments[], JSValueRef *exception)
{
    (void) function;
    FileClient *client = JSObjectGetPrivate(this_object);
    FileClientRequest *req = NULL;
    JSObjectRef promise = make_promise(client, exception, &req);
    if (promise == NULL) return JSValueMakeUndefined(context);

    if (argument_count < 1) {
        resolve_with(req, JSValueMakeBoolean(context, false));
        return promise;
    }
    char *rel_path = js_copy_string(context, arguments[0]);
    char *full_path = resolve_storage_path(client, rel_path, NULL);
    g_free(rel_path);
    if (full_path == NULL) {
        resolve_with(req, JSValueMakeBoolean(context, false));
        return promise;
    }
    gboolean exists = g_file_test(full_path, G_FILE_TEST_EXISTS);
    g_free(full_path);

    resolve_with(req, JSValueMakeBoolean(context, exists));
    return promise;
}

static JSValueRef
storage_list(JSContextRef context, JSObjectRef function, JSObjectRef this_object,
             size_t argument_count, const JSValueRef arguments[], JSValueRef *exception)
{
    (void) function;
    FileClient *client = JSObjectGetPrivate(this_object);
    FileClientRequest *req = NULL;
    JSObjectRef promise = make_promise(client, exception, &req);
    if (promise == NULL) return JSValueMakeUndefined(context);

    char *target_dir = NULL;
    if (argument_count >= 1 && !JSValueIsUndefined(context, arguments[0]) && !JSValueIsNull(context, arguments[0])) {
        char *rel = js_copy_string(context, arguments[0]);
        if (rel[0] != '\0') {
            target_dir = resolve_storage_path(client, rel, NULL);
        }
        g_free(rel);
    }
    if (target_dir == NULL) {
        target_dir = g_strdup(client->storage_root);
    }

    JSObjectRef array = JSObjectMakeArray(context, 0, NULL, NULL);
    if (g_file_test(target_dir, G_FILE_TEST_IS_DIR)) {
        GDir *dir = g_dir_open(target_dir, 0, NULL);
        if (dir != NULL) {
            const char *name = NULL;
            unsigned int idx = 0;
            while ((name = g_dir_read_name(dir)) != NULL) {
                JSObjectSetPropertyAtIndex(context, array, idx++, js_utf8(context, name), NULL);
            }
            g_dir_close(dir);
        }
    }
    g_free(target_dir);

    resolve_with(req, array);
    return promise;
}

static JSStaticFunction file_functions[] = {
    { "open", file_open, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "save", file_save, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "saveAs", file_save_as, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "read", storage_read, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "write", storage_write, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "delete", storage_delete, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "exists", storage_exists, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "list", storage_list, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { NULL, NULL, 0 }
};

static JSStaticFunction storage_functions[] = {
    { "read", storage_read, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "write", storage_write, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "delete", storage_delete, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "exists", storage_exists, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "list", storage_list, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { NULL, NULL, 0 }
};

FileClient *
file_client_new(JSGlobalContextRef context, const char *project_name)
{
    FileClient *client = g_new0(FileClient, 1);
    client->context = context;
    client->project_name = g_strdup(project_name ? project_name : "example");
    client->storage_root = g_build_filename(g_get_user_data_dir(), "allwhither", client->project_name, "storage", NULL);

    JSClassDefinition file_def = kJSClassDefinitionEmpty;
    file_def.className = "AllwhitherFile";
    file_def.staticFunctions = file_functions;
    JSClassRef file_class = JSClassCreate(&file_def);
    client->object = JSObjectMake(context, file_class, client);
    JSClassRelease(file_class);

    JSClassDefinition storage_def = kJSClassDefinitionEmpty;
    storage_def.className = "AllwhitherFileStorage";
    storage_def.staticFunctions = storage_functions;
    JSClassRef storage_class = JSClassCreate(&storage_def);
    client->storage_object = JSObjectMake(context, storage_class, client);
    JSClassRelease(storage_class);

    set_js_property(context, client->object, "storage", client->storage_object);

    return client;
}

void
file_client_set_window(FileClient *client, GtkWindow *window)
{
    if (client != NULL) {
        client->window = window;
    }
}

JSObjectRef
file_client_js_object(FileClient *client)
{
    return client != NULL ? client->object : NULL;
}

void
file_client_free(FileClient *client)
{
    if (client == NULL) return;
    g_free(client->project_name);
    g_free(client->storage_root);
    g_free(client);
}
