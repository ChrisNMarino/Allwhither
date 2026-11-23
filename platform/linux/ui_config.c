#include "ui_config.h"

#include "embedded_resources.h"

#include <string.h>

gboolean
ui_read_string_value(JsonObject *object, const char *member, const char **value)
{
    if (!json_object_has_member(object, member)) {
        return FALSE;
    }
    JsonNode *node = json_object_get_member(object, member);
    if (!JSON_NODE_HOLDS_VALUE(node) ||
        json_node_get_value_type(node) != G_TYPE_STRING) {
        return FALSE;
    }
    *value = json_node_get_string(node);
    return *value != NULL;
}

gboolean
ui_read_string(JsonObject *object, const char *member, const char **value)
{
    return ui_read_string_value(object, member, value) && **value != '\0';
}

gboolean
ui_read_integer(JsonObject *object, const char *member, int *value)
{
    if (!json_object_has_member(object, member)) {
        return FALSE;
    }
    JsonNode *node = json_object_get_member(object, member);
    if (!JSON_NODE_HOLDS_VALUE(node) ||
        json_node_get_value_type(node) != G_TYPE_INT64) {
        return FALSE;
    }
    gint64 number = json_node_get_int(node);
    if (number < G_MININT || number > G_MAXINT) {
        return FALSE;
    }
    *value = (int) number;
    return TRUE;
}

gboolean
ui_read_boolean(JsonObject *object, const char *member, gboolean *value)
{
    if (!json_object_has_member(object, member)) {
        return FALSE;
    }
    JsonNode *node = json_object_get_member(object, member);
    if (!JSON_NODE_HOLDS_VALUE(node) ||
        json_node_get_value_type(node) != G_TYPE_BOOLEAN) {
        return FALSE;
    }
    *value = json_node_get_boolean(node);
    return TRUE;
}

gboolean
ui_read_array(JsonObject *object, const char *member, JsonArray **array)
{
    if (!json_object_has_member(object, member)) {
        return FALSE;
    }
    JsonNode *node = json_object_get_member(object, member);
    if (!JSON_NODE_HOLDS_ARRAY(node)) {
        return FALSE;
    }
    *array = json_node_get_array(node);
    return TRUE;
}

static gboolean
has_string_array(JsonArray *array)
{
    if (array == NULL || json_array_get_length(array) == 0) {
        return FALSE;
    }
    for (guint index = 0; index < json_array_get_length(array); index++) {
        JsonNode *node = json_array_get_element(array, index);
        if (!JSON_NODE_HOLDS_VALUE(node) ||
            json_node_get_value_type(node) != G_TYPE_STRING ||
            json_node_get_string(node) == NULL ||
            *json_node_get_string(node) == '\0') {
            return FALSE;
        }
    }
    return TRUE;
}

static gboolean
is_valid_hex_color(const char *color)
{
    if (color == NULL || color[0] != '#' || strlen(color) != 7) {
        return FALSE;
    }
    for (guint index = 1; index < 7; index++) {
        if (!g_ascii_isxdigit(color[index])) {
            return FALSE;
        }
    }
    return TRUE;
}

static gboolean
color_member_is_valid(JsonObject *object, const char *member)
{
    if (!json_object_has_member(object, member)) {
        return TRUE;
    }
    const char *color;
    return ui_read_string(object, member, &color) && is_valid_hex_color(color);
}

static gboolean
is_javascript_identifier(const char *value)
{
    if (value == NULL || *value == '\0' ||
        !(g_ascii_isalpha(*value) || *value == '_' || *value == '$')) {
        return FALSE;
    }
    for (const char *character = value + 1; *character != '\0'; character++) {
        if (!(g_ascii_isalnum(*character) || *character == '_' || *character == '$')) {
            return FALSE;
        }
    }
    return TRUE;
}

static gboolean
validate_actions(JsonObject *control, const char *type)
{
    if (!json_object_has_member(control, "actions")) {
        return TRUE;
    }
    JsonNode *actions_node = json_object_get_member(control, "actions");
    if (!JSON_NODE_HOLDS_OBJECT(actions_node)) {
        return FALSE;
    }
    JsonObject *actions = json_node_get_object(actions_node);
    GList *events = json_object_get_members(actions);
    gboolean valid = TRUE;
    for (GList *event = events; valid && event != NULL; event = event->next) {
        const char *name = event->data;
        gboolean supported =
            (g_str_equal(type, "button") && g_str_equal(name, "click")) ||
            ((g_str_equal(type, "comboBox") || g_str_equal(type, "checkbox") ||
              g_str_equal(type, "radioGroup") || g_str_equal(type, "list")) &&
             g_str_equal(name, "change")) ||
            ((g_str_equal(type, "textField") || g_str_equal(type, "textArea") ||
              g_str_equal(type, "slider")) && g_str_equal(name, "input"));
        JsonNode *action_node = json_object_get_member(actions, name);
        if (!supported || !JSON_NODE_HOLDS_OBJECT(action_node)) {
            valid = FALSE;
            break;
        }
        JsonObject *action = json_node_get_object(action_node);
        const char *file;
        const char *function;
        if (!ui_read_string(action, "file", &file) ||
            !ui_read_string(action, "function", &function)) {
            valid = FALSE;
            break;
        }
        char *normalized = g_strdup(file);
        for (char *character = normalized; *character != '\0'; character++) {
            if (*character == '\\') *character = '/';
        }
        char *extension = strrchr(normalized, '.');
        gboolean valid_path = extension != NULL &&
            g_ascii_strcasecmp(extension, ".js") == 0 &&
            !g_path_is_absolute(normalized) &&
            !(g_ascii_isalpha(normalized[0]) && normalized[1] == ':');
        char **segments = g_strsplit(normalized, "/", -1);
        for (guint index = 0; valid_path && segments[index] != NULL; index++) {
            valid_path = !g_str_equal(segments[index], "..");
        }
        valid = valid_path && is_javascript_identifier(function);
        g_strfreev(segments);
        g_free(normalized);
    }
    g_list_free(events);
    return valid;
}

static gboolean
validate_control(JsonObject *control)
{
    const char *type;
    if (control == NULL || !ui_read_string(control, "type", &type)) {
        return FALSE;
    }
    if (!color_member_is_valid(control, "backgroundColor") ||
        !color_member_is_valid(control, "foregroundColor")) {
        return FALSE;
    }
    if (!validate_actions(control, type)) {
        return FALSE;
    }

    if (g_str_equal(type, "label")) {
        const char *text;
        return ui_read_string(control, "text", &text);
    }
    if (g_str_equal(type, "button")) {
        const char *text;
        const char *clicked_text;
        return ui_read_string(control, "text", &text) &&
               ui_read_string(control, "clickedText", &clicked_text);
    }
    if (g_str_equal(type, "comboBox") ||
        g_str_equal(type, "radioGroup") ||
        g_str_equal(type, "list")) {
        const char *label;
        JsonArray *options;
        int selected_index;
        return ui_read_string(control, "label", &label) &&
               ui_read_array(control, "options", &options) &&
               has_string_array(options) &&
               ui_read_integer(control, "selectedIndex", &selected_index) &&
               selected_index >= 0 &&
               (guint) selected_index < json_array_get_length(options);
    }
    if (g_str_equal(type, "textField")) {
        const char *label;
        const char *value;
        const char *placeholder;
        return ui_read_string(control, "label", &label) &&
               ui_read_string_value(control, "value", &value) &&
               (!json_object_has_member(control, "placeholder") ||
                ui_read_string_value(control, "placeholder", &placeholder));
    }
    if (g_str_equal(type, "textArea")) {
        const char *label;
        const char *value;
        return ui_read_string(control, "label", &label) &&
               ui_read_string_value(control, "value", &value);
    }
    if (g_str_equal(type, "checkbox")) {
        const char *label;
        gboolean checked;
        return ui_read_string(control, "label", &label) &&
               ui_read_boolean(control, "checked", &checked);
    }
    if (g_str_equal(type, "slider")) {
        const char *label;
        int minimum;
        int maximum;
        int value;
        return ui_read_string(control, "label", &label) &&
               ui_read_integer(control, "min", &minimum) &&
               ui_read_integer(control, "max", &maximum) &&
               ui_read_integer(control, "value", &value) &&
               minimum < maximum && value >= minimum && value <= maximum;
    }
    if (g_str_equal(type, "scrollPanel")) {
        const char *label;
        int height;
        JsonArray *children;
        if (!ui_read_string(control, "label", &label) ||
            !ui_read_integer(control, "height", &height) || height <= 0 ||
            !ui_read_array(control, "children", &children)) {
            return FALSE;
        }
        for (guint index = 0; index < json_array_get_length(children); index++) {
            JsonNode *child = json_array_get_element(children, index);
            if (!JSON_NODE_HOLDS_VALUE(child) ||
                json_node_get_value_type(child) != G_TYPE_STRING ||
                json_node_get_string(child) == NULL ||
                *json_node_get_string(child) == '\0') {
                return FALSE;
            }
        }
        return TRUE;
    }
    if (g_str_equal(type, "panel")) {
        const char *label;
        JsonArray *children;
        if (!ui_read_string(control, "label", &label) ||
            !ui_read_array(control, "children", &children)) {
            return FALSE;
        }
        for (guint index = 0; index < json_array_get_length(children); index++) {
            JsonNode *child = json_array_get_element(children, index);
            if (!JSON_NODE_HOLDS_VALUE(child) ||
                json_node_get_value_type(child) != G_TYPE_STRING ||
                json_node_get_string(child) == NULL ||
                *json_node_get_string(child) == '\0') {
                return FALSE;
            }
        }
        return TRUE;
    }
    return FALSE;
}

static gboolean
visit_control(const char *id,
              JsonObject *controls,
              GHashTable *visited,
              GHashTable *visiting)
{
    if (id == NULL || *id == '\0' ||
        !json_object_has_member(controls, id) ||
        g_hash_table_contains(visiting, id) ||
        g_hash_table_contains(visited, id)) {
        return FALSE;
    }
    JsonNode *node = json_object_get_member(controls, id);
    if (!JSON_NODE_HOLDS_OBJECT(node)) {
        return FALSE;
    }
    JsonObject *control = json_node_get_object(node);
    if (!validate_control(control)) {
        return FALSE;
    }

    g_hash_table_add(visiting, (gpointer) id);
    const char *type;
    ui_read_string(control, "type", &type);
    if (g_str_equal(type, "scrollPanel") || g_str_equal(type, "panel")) {
        JsonArray *children;
        ui_read_array(control, "children", &children);
        for (guint index = 0; index < json_array_get_length(children); index++) {
            const char *child_id = json_node_get_string(json_array_get_element(children, index));
            if (!visit_control(child_id, controls, visited, visiting)) {
                g_hash_table_remove(visiting, id);
                return FALSE;
            }
        }
    }
    g_hash_table_remove(visiting, id);
    g_hash_table_add(visited, (gpointer) id);
    return TRUE;
}

static gboolean
validate_control_tree(JsonObject *controls, JsonArray *order)
{
    if (controls == NULL || order == NULL) {
        return FALSE;
    }
    GHashTable *visited = g_hash_table_new(g_str_hash, g_str_equal);
    GHashTable *visiting = g_hash_table_new(g_str_hash, g_str_equal);
    gboolean valid = TRUE;
    for (guint index = 0; index < json_array_get_length(order); index++) {
        JsonNode *node = json_array_get_element(order, index);
        if (!JSON_NODE_HOLDS_VALUE(node) ||
            json_node_get_value_type(node) != G_TYPE_STRING ||
            !visit_control(json_node_get_string(node), controls, visited, visiting)) {
            valid = FALSE;
            break;
        }
    }
    GList *ids = json_object_get_members(controls);
    for (GList *item = ids; valid && item != NULL; item = item->next) {
        if (g_hash_table_contains(visited, item->data)) {
            continue;
        }
        JsonObject *control = json_object_get_object_member(controls, item->data);
        const char *type = NULL;
        if (control == NULL ||
            !ui_read_string(control, "type", &type) ||
            !g_str_equal(type, "panel")) {
            continue;
        }
        if (!visit_control(item->data, controls, visited, visiting)) {
            valid = FALSE;
        }
    }
    for (GList *item = ids; valid && item != NULL; item = item->next) {
        valid = g_hash_table_contains(visited, item->data);
    }
    g_list_free(ids);
    g_hash_table_unref(visited);
    g_hash_table_unref(visiting);
    return valid;
}

static gboolean
validate_menus(JsonArray *menus)
{
    if (menus == NULL) {
        return FALSE;
    }
    for (guint menu_index = 0; menu_index < json_array_get_length(menus); menu_index++) {
        JsonNode *menu_node = json_array_get_element(menus, menu_index);
        if (!JSON_NODE_HOLDS_OBJECT(menu_node)) {
            return FALSE;
        }
        JsonObject *menu = json_node_get_object(menu_node);
        const char *label;
        JsonArray *items;
        if (!ui_read_string(menu, "label", &label) ||
            !ui_read_array(menu, "items", &items) ||
            json_array_get_length(items) == 0) {
            return FALSE;
        }
        for (guint item_index = 0; item_index < json_array_get_length(items); item_index++) {
            JsonNode *item_node = json_array_get_element(items, item_index);
            if (!JSON_NODE_HOLDS_OBJECT(item_node)) {
                return FALSE;
            }
            JsonObject *item = json_node_get_object(item_node);
            const char *text;
            const char *message;
            if (!ui_read_string(item, "text", &text) ||
                !ui_read_string(item, "message", &message)) {
                return FALSE;
            }
        }
    }
    return TRUE;
}

static void
merge_json_object(JsonObject *target, JsonObject *source)
{
    GList *members = json_object_get_members(source);
    for (GList *member = members; member != NULL; member = member->next) {
        const char *key = member->data;
        if (g_str_equal(key, "includes") || g_str_equal(key, "scripts")) {
            continue;
        }
        JsonNode *value = json_object_get_member(source, key);
        JsonNode *existing = json_object_get_member(target, key);
        if (existing != NULL && value != NULL &&
            JSON_NODE_HOLDS_OBJECT(existing) && JSON_NODE_HOLDS_OBJECT(value)) {
            merge_json_object(json_node_get_object(existing), json_node_get_object(value));
            continue;
        }
        json_object_set_member(target, key, json_node_copy(value));
    }
    g_list_free(members);
}

static gboolean
script_listed(JsonArray *scripts, const char *path)
{
    for (guint index = 0; index < json_array_get_length(scripts); index++) {
        const char *existing = json_array_get_string_element(scripts, index);
        if (existing != NULL && g_str_equal(existing, path)) {
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean
collect_merged_scripts(JsonArray *scripts, JsonObject *included)
{
    if (!json_object_has_member(included, "scripts")) {
        return TRUE;
    }
    JsonNode *node = json_object_get_member(included, "scripts");
    if (!JSON_NODE_HOLDS_ARRAY(node)) {
        return TRUE;
    }
    JsonArray *declared = json_node_get_array(node);
    for (guint index = 0; index < json_array_get_length(declared); index++) {
        const char *path = json_array_get_string_element(declared, index);
        if (path != NULL && !script_listed(scripts, path)) {
            json_array_add_string_element(scripts, path);
        }
    }
    return TRUE;
}

static gboolean
collect_declared_scripts(JsonArray *scripts,
                         JsonObject *source,
                         const char *base_path,
                         GError **error)
{
    if (!json_object_has_member(source, "scripts")) {
        return TRUE;
    }
    JsonNode *node = json_object_get_member(source, "scripts");
    if (!JSON_NODE_HOLDS_ARRAY(node)) {
        g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                    "scripts must be an array");
        return FALSE;
    }
    JsonArray *declared = json_node_get_array(node);
    for (guint index = 0; index < json_array_get_length(declared); index++) {
        JsonNode *item = json_array_get_element(declared, index);
        if (!JSON_NODE_HOLDS_VALUE(item) ||
            json_node_get_value_type(item) != G_TYPE_STRING ||
            json_node_get_string(item) == NULL ||
            *json_node_get_string(item) == '\0') {
            g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                        "scripts must contain non-empty paths");
            return FALSE;
        }
        char *resolved = omni_resource_resolve(
            base_path, json_node_get_string(item), error);
        if (resolved == NULL) {
            return FALSE;
        }
        const char *extension = strrchr(resolved, '.');
        if (extension == NULL || g_ascii_strcasecmp(extension, ".js") != 0) {
            g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                        "Script files must be relative .js paths without parent segments.");
            g_free(resolved);
            return FALSE;
        }
        if (!script_listed(scripts, resolved)) {
            json_array_add_string_element(scripts, resolved);
        }
        g_free(resolved);
    }
    return TRUE;
}

static JsonObject *
load_configuration_file(const char *path, GHashTable *include_stack, GError **error)
{
    char *full_path = omni_resource_normalize(path, error);
    if (full_path == NULL) {
        return NULL;
    }
    if (g_hash_table_contains(include_stack, full_path)) {
        g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                    "Configuration include cycle at %s", full_path);
        g_free(full_path);
        return NULL;
    }
    g_hash_table_add(include_stack, g_strdup(full_path));

    char *text = NULL;
    gsize length = 0;
    JsonParser *parser = NULL;
    JsonObject *merged = NULL;
    JsonArray *scripts = NULL;
    text = omni_resource_read(full_path, &length, error);
    if (text == NULL) {
        goto fail;
    }
    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, text, (gssize)length, error)) {
        goto fail;
    }
    g_clear_pointer(&text, g_free);

    JsonNode *root_node = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root_node)) {
        g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                    "Configuration %s must contain a JSON object", full_path);
        goto fail;
    }

    JsonObject *source = json_node_get_object(root_node);
    merged = json_object_new();
    scripts = json_array_new();
    if (json_object_has_member(source, "includes")) {
        JsonNode *includes_node = json_object_get_member(source, "includes");
        if (!JSON_NODE_HOLDS_ARRAY(includes_node)) {
            g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                        "includes must be an array");
            goto fail;
        }
        JsonArray *includes = json_node_get_array(includes_node);
        for (guint index = 0; index < json_array_get_length(includes); index++) {
            JsonNode *include_node = json_array_get_element(includes, index);
            if (!JSON_NODE_HOLDS_VALUE(include_node) ||
                json_node_get_value_type(include_node) != G_TYPE_STRING ||
                json_node_get_string(include_node) == NULL ||
                *json_node_get_string(include_node) == '\0') {
                g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                            "includes must contain non-empty paths");
                goto fail;
            }
            char *include_path = omni_resource_resolve(
                full_path, json_node_get_string(include_node), error);
            if (include_path == NULL) {
                goto fail;
            }
            JsonObject *included = load_configuration_file(include_path, include_stack, error);
            g_free(include_path);
            if (included == NULL) {
                goto fail;
            }
            collect_merged_scripts(scripts, included);
            merge_json_object(merged, included);
            json_object_unref(included);
        }
    }

    merge_json_object(merged, source);
    if (!collect_declared_scripts(scripts, source, full_path, error)) {
        goto fail;
    }
    json_object_set_array_member(merged, "scripts", scripts);
    scripts = NULL;
    g_hash_table_remove(include_stack, full_path);
    g_free(full_path);
    g_object_unref(parser);
    return merged;

fail:
    g_hash_table_remove(include_stack, full_path);
    g_free(full_path);
    g_free(text);
    if (parser != NULL) {
        g_object_unref(parser);
    }
    if (scripts != NULL) {
        g_object_ref_sink(scripts);
        json_array_unref(scripts);
    }
    if (merged != NULL) {
        json_object_unref(merged);
    }
    return NULL;
}

JsonObject *
ui_config_load(const char *path, GError **error)
{
    GHashTable *include_stack = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    JsonObject *root = load_configuration_file(path, include_stack, error);
    g_hash_table_unref(include_stack);
    return root;
}

gboolean
ui_config_validate(JsonObject *root,
                   const char **title,
                   int *width,
                   int *height,
                   JsonArray **menus,
                   JsonObject **controls,
                   JsonArray **order)
{
    if (root == NULL || !json_object_has_member(root, "ui")) {
        return FALSE;
    }
    JsonNode *ui_node = json_object_get_member(root, "ui");
    if (!JSON_NODE_HOLDS_OBJECT(ui_node)) {
        return FALSE;
    }
    JsonObject *ui = json_node_get_object(ui_node);
    if (!json_object_has_member(ui, "window") ||
        !JSON_NODE_HOLDS_OBJECT(json_object_get_member(ui, "window"))) {
        return FALSE;
    }
    JsonObject *window = json_object_get_object_member(ui, "window");
    if (!json_object_has_member(window, "layout") ||
        !JSON_NODE_HOLDS_OBJECT(json_object_get_member(window, "layout"))) {
        return FALSE;
    }
    JsonObject *layout = json_object_get_object_member(window, "layout");
    const char *layout_type;
    gboolean fullscreen;
    if (!ui_read_string(layout, "type", &layout_type) ||
        (!g_str_equal(layout_type, "vertical") &&
         !g_str_equal(layout_type, "center")) ||
        !ui_read_string(window, "title", title) ||
        !ui_read_integer(window, "width", width) || *width <= 0 ||
        !ui_read_integer(window, "height", height) || *height <= 0 ||
        (json_object_has_member(window, "fullscreen") &&
         !ui_read_boolean(window, "fullscreen", &fullscreen)) ||
        !ui_read_array(ui, "menus", menus) ||
        !json_object_has_member(ui, "controls") ||
        !JSON_NODE_HOLDS_OBJECT(json_object_get_member(ui, "controls")) ||
        !ui_read_array(layout, "children", order)) {
        return FALSE;
    }
    if (!color_member_is_valid(window, "backgroundColor") ||
        !color_member_is_valid(window, "foregroundColor")) {
        return FALSE;
    }
    if (json_object_has_member(ui, "theme")) {
        JsonNode *theme_node = json_object_get_member(ui, "theme");
        if (!JSON_NODE_HOLDS_OBJECT(theme_node)) {
            return FALSE;
        }
        JsonObject *theme = json_node_get_object(theme_node);
        if (!color_member_is_valid(theme, "backgroundColor") ||
            !color_member_is_valid(theme, "foregroundColor")) {
            return FALSE;
        }
    }
    *controls = json_object_get_object_member(ui, "controls");
    return validate_menus(*menus) && validate_control_tree(*controls, *order);
}
