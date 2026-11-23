#include "embedded_resources.h"

#include <gio/gio.h>
#include <string.h>

char *
omni_resource_normalize(const char *relative_path, GError **error)
{
    if (relative_path == NULL || *relative_path == '\0') {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME,
                    "Resource path must be a non-empty relative path.");
        return NULL;
    }

    char *copy = g_strdup(relative_path);
    for (char *character = copy; *character != '\0'; character++) {
        if (*character == '\\') *character = '/';
    }
    if (copy[0] == '/' || (g_ascii_isalpha(copy[0]) && copy[1] == ':')) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME,
                    "Resource path '%s' must stay inside the project.",
                    relative_path);
        g_free(copy);
        return NULL;
    }

    char **segments = g_strsplit(copy, "/", -1);
    g_free(copy);
    GPtrArray *parts = g_ptr_array_new_with_free_func(g_free);
    for (guint index = 0; segments[index] != NULL; index++) {
        if (segments[index][0] == '\0' || g_str_equal(segments[index], ".")) {
            continue;
        }
        if (g_str_equal(segments[index], "..")) {
            if (parts->len == 0) {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME,
                            "Resource path '%s' must stay inside the project.",
                            relative_path);
                g_ptr_array_free(parts, TRUE);
                g_strfreev(segments);
                return NULL;
            }
            g_ptr_array_remove_index(parts, parts->len - 1);
            continue;
        }
        g_ptr_array_add(parts, g_strdup(segments[index]));
    }
    g_strfreev(segments);
    if (parts->len == 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME,
                    "Resource path must be a non-empty relative path.");
        g_ptr_array_free(parts, TRUE);
        return NULL;
    }

    g_ptr_array_add(parts, NULL);
    char *joined = g_strjoinv("/", (char **)parts->pdata);
    g_ptr_array_free(parts, TRUE);
    return joined;
}

char *
omni_resource_resolve(const char *current_resource,
                      const char *relative_path,
                      GError **error)
{
    char *normalized_current = omni_resource_normalize(current_resource, error);
    if (normalized_current == NULL) {
        return NULL;
    }
    const char *slash = strrchr(normalized_current, '/');
    char *combined = slash == NULL
        ? g_strdup(relative_path)
        : g_strdup_printf("%.*s/%s", (int)(slash - normalized_current),
                          normalized_current, relative_path);
    g_free(normalized_current);
    char *resolved = omni_resource_normalize(combined, error);
    g_free(combined);
    return resolved;
}

char *
omni_resource_read(const char *relative_path, gsize *length, GError **error)
{
    char *normalized = omni_resource_normalize(relative_path, error);
    if (normalized == NULL) {
        return NULL;
    }
    for (const OmniEmbeddedResource *resource = omni_embedded_resources;
         resource->path != NULL;
         resource++) {
        if (!g_str_equal(resource->path, normalized)) {
            continue;
        }
        char *text = g_malloc(resource->length + 1);
        for (gsize index = 0; index < resource->length; index++) {
            unsigned char mask = omni_embedded_key[index % 16] ^
                (unsigned char)((index * 13u + 7u) & 0xFFu);
            text[index] = (char)(resource->data[index] ^ mask);
        }
        text[resource->length] = '\0';
        if (length != NULL) {
            *length = resource->length;
        }
        g_free(normalized);
        return text;
    }

    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                "Embedded resource '%s' was not found.", normalized);
    g_free(normalized);
    return NULL;
}
