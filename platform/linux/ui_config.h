#ifndef ALLWHITHER_UI_CONFIG_H
#define ALLWHITHER_UI_CONFIG_H

#include <json-glib/json-glib.h>

JsonObject *ui_config_load(const char *path, GError **error);
gboolean ui_config_validate(JsonObject *root,
                            const char **title,
                            int *width,
                            int *height,
                            JsonArray **menus,
                            JsonObject **controls,
                            JsonArray **order);

gboolean ui_layout_validate(JsonObject *layout, gboolean allow_center);
const char *ui_layout_resolve(JsonObject *layout, int width);
gboolean ui_control_media_validate(JsonArray *media);
gboolean ui_control_media_visible(JsonArray *media, int width);

gboolean ui_read_string_value(JsonObject *object, const char *member, const char **value);
gboolean ui_read_string(JsonObject *object, const char *member, const char **value);
gboolean ui_read_integer(JsonObject *object, const char *member, int *value);
gboolean ui_read_boolean(JsonObject *object, const char *member, gboolean *value);
gboolean ui_read_array(JsonObject *object, const char *member, JsonArray **array);
gboolean ui_read_dimension(JsonObject *object,
                           const char *member,
                           gboolean *is_percent,
                           double *value);

#endif
