#ifndef ALLWHITHER_EMBEDDED_RESOURCES_H
#define ALLWHITHER_EMBEDDED_RESOURCES_H

#include <glib.h>

typedef struct {
    const char *path;
    const unsigned char *data;
    gsize length;
} AllwhitherEmbeddedResource;

extern const unsigned char allwhither_embedded_key[16];
extern const AllwhitherEmbeddedResource allwhither_embedded_resources[];

char *allwhither_resource_normalize(const char *relative_path, GError **error);
char *allwhither_resource_resolve(const char *current_resource,
                            const char *relative_path,
                            GError **error);
char *allwhither_resource_read(const char *relative_path, gsize *length, GError **error);

#endif
