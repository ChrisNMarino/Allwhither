#ifndef OMNI_EMBEDDED_RESOURCES_H
#define OMNI_EMBEDDED_RESOURCES_H

#include <glib.h>

typedef struct {
    const char *path;
    const unsigned char *data;
    gsize length;
} OmniEmbeddedResource;

extern const unsigned char omni_embedded_key[16];
extern const OmniEmbeddedResource omni_embedded_resources[];

char *omni_resource_normalize(const char *relative_path, GError **error);
char *omni_resource_resolve(const char *current_resource,
                            const char *relative_path,
                            GError **error);
char *omni_resource_read(const char *relative_path, gsize *length, GError **error);

#endif
