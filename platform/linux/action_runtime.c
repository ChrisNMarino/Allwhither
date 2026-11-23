#include "action_runtime.h"

#include "control_renderer.h"
#include "crypto_client.h"
#include "embedded_resources.h"
#include "file_client.h"
#include "http_client.h"

#include <JavaScriptCore/JavaScript.h>
#include <string.h>

#ifndef OMNI_PROJECT_NAME
#define OMNI_PROJECT_NAME "example"
#endif

struct _ActionRuntime {
    JSGlobalContextRef context;
    JSObjectRef dom;
    HttpClient *http;
    FileClient *file;
    CryptoClient *crypto;
    JsonObject *controls;
    GtkWidget *status;
    GtkWidget *container;
    gboolean centered;
    GHashTable *elements;
    GHashTable *loaded_files;
    gboolean invoking;
};

#define DOM_CALLBACK(name) \
    static JSValueRef name(JSContextRef, JSObjectRef, JSObjectRef, size_t, \
                           const JSValueRef[], JSValueRef *)
DOM_CALLBACK(dom_get_text);
DOM_CALLBACK(dom_set_text);
DOM_CALLBACK(dom_get_value);
DOM_CALLBACK(dom_set_value);
DOM_CALLBACK(dom_get_checked);
DOM_CALLBACK(dom_set_checked);
DOM_CALLBACK(dom_get_secure);
DOM_CALLBACK(dom_set_secure);
DOM_CALLBACK(dom_set_status);
DOM_CALLBACK(dom_add_panel);
DOM_CALLBACK(dom_remove_panel);
DOM_CALLBACK(dom_set_options);
DOM_CALLBACK(dom_get_selected_index);
#undef DOM_CALLBACK

static JSValueRef
api_platform(JSContextRef context,
             JSObjectRef function,
             JSObjectRef this_object,
             size_t argument_count,
             const JSValueRef arguments[],
             JSValueRef *exception)
{
    (void) function;
    (void) this_object;
    (void) argument_count;
    (void) arguments;
    (void) exception;
    JSStringRef value = JSStringCreateWithUTF8CString("linux");
    JSValueRef result = JSValueMakeString(context, value);
    JSStringRelease(value);
    return result;
}

static JSStaticFunction api_functions[] = {
    { "platform", api_platform, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { NULL, NULL, 0 }
};

static JSStaticFunction dom_functions[] = {
    { "getText", dom_get_text, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "setText", dom_set_text, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "getValue", dom_get_value, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "setValue", dom_set_value, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "getChecked", dom_get_checked, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "setChecked", dom_set_checked, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "getSecure", dom_get_secure, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "setSecure", dom_set_secure, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "setStatus", dom_set_status, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "addPanel", dom_add_panel, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "removePanel", dom_remove_panel, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "setOptions", dom_set_options, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "getSelectedIndex", dom_get_selected_index, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { NULL, NULL, 0 }
};

static char *
js_string(JSContextRef context, JSValueRef value)
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

static void
set_exception(JSContextRef context, JSValueRef *exception, const char *message)
{
    if (exception != NULL) {
        JSStringRef string = JSStringCreateWithUTF8CString(message);
        *exception = JSValueMakeString(context, string);
        JSStringRelease(string);
    }
}

static GtkWidget *
get_element(ActionRuntime *runtime, const char *id)
{
    GtkWidget *widget = g_hash_table_lookup(runtime->elements, id);
    return widget;
}

static GtkWidget *
find_widget(GtkWidget *root, GType type)
{
    if (G_TYPE_CHECK_INSTANCE_TYPE(root, type)) {
        return root;
    }
    for (GtkWidget *child = gtk_widget_get_first_child(root);
         child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
        GtkWidget *found = find_widget(child, type);
        if (found != NULL) {
            return found;
        }
    }
    return NULL;
}

static const char *
control_type(ActionRuntime *runtime, const char *id)
{
    JsonObject *spec = json_object_get_object_member(runtime->controls, id);
    return json_object_get_string_member(spec, "type");
}

static const char *
get_text(ActionRuntime *runtime, const char *id)
{
    GtkWidget *widget = get_element(runtime, id);
    const char *type = control_type(runtime, id);
    if (g_str_equal(type, "label")) {
        return gtk_label_get_text(GTK_LABEL(widget));
    }
    if (g_str_equal(type, "button")) {
        return gtk_button_get_label(GTK_BUTTON(widget));
    }
    if (g_str_equal(type, "checkbox")) {
        return gtk_check_button_get_label(GTK_CHECK_BUTTON(widget));
    }
    if (g_str_equal(type, "radioGroup") || g_str_equal(type, "scrollPanel") ||
        g_str_equal(type, "panel")) {
        GtkWidget *caption = gtk_widget_get_first_child(widget);
        return GTK_IS_LABEL(caption) ? gtk_label_get_text(GTK_LABEL(caption)) : "";
    }
    GtkWidget *caption = find_widget(widget, GTK_TYPE_LABEL);
    return caption == NULL ? "" : gtk_label_get_text(GTK_LABEL(caption));
}

static void
set_text(ActionRuntime *runtime, const char *id, const char *text)
{
    GtkWidget *widget = get_element(runtime, id);
    const char *type = control_type(runtime, id);
    if (g_str_equal(type, "label")) {
        gtk_label_set_text(GTK_LABEL(widget), text);
    } else if (g_str_equal(type, "button")) {
        gtk_button_set_label(GTK_BUTTON(widget), text);
    } else if (g_str_equal(type, "checkbox")) {
        gtk_check_button_set_label(GTK_CHECK_BUTTON(widget), text);
    } else {
        GtkWidget *caption = find_widget(widget, GTK_TYPE_LABEL);
        if (caption == NULL) {
            g_warning("Control '%s' does not expose writable text", id);
            return;
        }
        gtk_label_set_text(GTK_LABEL(caption), text);
    }
}

static char *
get_value(ActionRuntime *runtime, const char *id)
{
    GtkWidget *root = get_element(runtime, id);
    const char *type = control_type(runtime, id);
    GtkWidget *widget;
    if (g_str_equal(type, "textField")) {
        widget = find_widget(root, GTK_TYPE_ENTRY);
        return widget == NULL ? NULL : g_strdup(gtk_editable_get_text(GTK_EDITABLE(widget)));
    }
    if (g_str_equal(type, "textArea")) {
        widget = find_widget(root, GTK_TYPE_TEXT_VIEW);
        if (widget == NULL) return NULL;
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget));
        GtkTextIter start;
        GtkTextIter end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    }
    if (g_str_equal(type, "comboBox")) {
        widget = find_widget(root, GTK_TYPE_DROP_DOWN);
        if (widget == NULL) return NULL;
        guint index = gtk_drop_down_get_selected(GTK_DROP_DOWN(widget));
        JsonArray *options = json_object_get_array_member(
            json_object_get_object_member(runtime->controls, id), "options");
        return index >= json_array_get_length(options)
            ? NULL
            : g_strdup(json_node_get_string(json_array_get_element(options, index)));
    }
    if (g_str_equal(type, "list")) {
        widget = find_widget(root, GTK_TYPE_LIST_BOX);
        if (widget == NULL) return NULL;
        GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(widget));
        if (row == NULL) return NULL;
        GtkWidget *label = gtk_list_box_row_get_child(row);
        return g_strdup(gtk_label_get_text(GTK_LABEL(label)));
    }
    if (g_str_equal(type, "slider")) {
        widget = find_widget(root, GTK_TYPE_SCALE);
        if (widget == NULL) return NULL;
        return g_strdup_printf("%d", (int) gtk_range_get_value(GTK_RANGE(widget)));
    }
    if (g_str_equal(type, "radioGroup")) {
        widget = find_widget(root, GTK_TYPE_CHECK_BUTTON);
        while (widget != NULL) {
            if (gtk_check_button_get_active(GTK_CHECK_BUTTON(widget))) {
                return g_strdup(gtk_check_button_get_label(GTK_CHECK_BUTTON(widget)));
            }
            GtkWidget *next = gtk_widget_get_next_sibling(widget);
            widget = next != NULL && GTK_IS_CHECK_BUTTON(next) ? next : NULL;
        }
    }
    return NULL;
}

static gboolean
set_value(ActionRuntime *runtime, const char *id, const char *value)
{
    GtkWidget *root = get_element(runtime, id);
    const char *type = control_type(runtime, id);
    GtkWidget *widget;
    if (g_str_equal(type, "textField")) {
        widget = find_widget(root, GTK_TYPE_ENTRY);
        if (widget != NULL) gtk_editable_set_text(GTK_EDITABLE(widget), value);
        return widget != NULL;
    }
    if (g_str_equal(type, "textArea")) {
        widget = find_widget(root, GTK_TYPE_TEXT_VIEW);
        if (widget != NULL) {
            gtk_text_buffer_set_text(
                gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget)), value, -1);
        }
        return widget != NULL;
    }
    if (g_str_equal(type, "comboBox")) {
        widget = find_widget(root, GTK_TYPE_DROP_DOWN);
        if (widget == NULL) return FALSE;
        JsonArray *options = json_object_get_array_member(
            json_object_get_object_member(runtime->controls, id), "options");
        for (guint index = 0; index < json_array_get_length(options); index++) {
            if (g_str_equal(json_node_get_string(json_array_get_element(options, index)),
                            value)) {
                gtk_drop_down_set_selected(GTK_DROP_DOWN(widget), index);
                return TRUE;
            }
        }
        return FALSE;
    }
    if (g_str_equal(type, "slider")) {
        char *end = NULL;
        double parsed = g_ascii_strtod(value, &end);
        widget = find_widget(root, GTK_TYPE_SCALE);
        if (widget == NULL || end == value || *end != '\0') return FALSE;
        gtk_range_set_value(GTK_RANGE(widget), parsed);
        return TRUE;
    }
    if (g_str_equal(type, "list")) {
        widget = find_widget(root, GTK_TYPE_LIST_BOX);
        if (widget == NULL) return FALSE;
        for (int index = 0; ; index++) {
            GtkListBoxRow *row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(widget), index);
            if (row == NULL) return FALSE;
            GtkWidget *label = gtk_list_box_row_get_child(row);
            if (g_str_equal(gtk_label_get_text(GTK_LABEL(label)), value)) {
                gtk_list_box_select_row(GTK_LIST_BOX(widget), row);
                return TRUE;
            }
        }
    }
    if (g_str_equal(type, "radioGroup")) {
        widget = find_widget(root, GTK_TYPE_CHECK_BUTTON);
        while (widget != NULL) {
            if (g_str_equal(gtk_check_button_get_label(GTK_CHECK_BUTTON(widget)), value)) {
                gtk_check_button_set_active(GTK_CHECK_BUTTON(widget), TRUE);
                return TRUE;
            }
            GtkWidget *next = gtk_widget_get_next_sibling(widget);
            widget = next != NULL && GTK_IS_CHECK_BUTTON(next) ? next : NULL;
        }
    }
    return FALSE;
}

static JSValueRef
dom_get_text(JSContextRef context,
             JSObjectRef function,
             JSObjectRef this_object,
             size_t count,
             const JSValueRef arguments[],
             JSValueRef *exception)
{
    (void) function;
    if (count < 1) return JSValueMakeUndefined(context);
    ActionRuntime *runtime = JSObjectGetPrivate(this_object);
    char *id = js_string(context, arguments[0]);
    if (json_object_get_member(runtime->controls, id) == NULL ||
        get_element(runtime, id) == NULL) {
        char *message = g_strdup_printf("Unknown or unrendered control id: %s", id);
        set_exception(context, exception, message);
        g_free(message);
        g_free(id);
        return JSValueMakeUndefined(context);
    }
    const char *text = get_text(runtime, id);
    JSStringRef value = JSStringCreateWithUTF8CString(text == NULL ? "" : text);
    JSValueRef result = JSValueMakeString(context, value);
    JSStringRelease(value);
    g_free(id);
    (void) exception;
    return result;
}

static JSValueRef
dom_set_text(JSContextRef context,
             JSObjectRef function,
             JSObjectRef this_object,
             size_t count,
             const JSValueRef arguments[],
             JSValueRef *exception)
{
    (void) function;
    if (count < 2) {
        set_exception(context, exception, "setText requires a control id and text.");
        return JSValueMakeUndefined(context);
    }
    ActionRuntime *runtime = JSObjectGetPrivate(this_object);
    char *id = js_string(context, arguments[0]);
    char *text = js_string(context, arguments[1]);
    if (json_object_get_member(runtime->controls, id) == NULL ||
        get_element(runtime, id) == NULL) {
        char *message = g_strdup_printf("Unknown or unrendered control id: %s", id);
        set_exception(context, exception, message);
        g_free(message);
        g_free(id);
        g_free(text);
        return JSValueMakeUndefined(context);
    }
    set_text(runtime, id, text);
    g_free(id);
    g_free(text);
    return JSValueMakeUndefined(context);
}

static JSValueRef
dom_get_value(JSContextRef context,
              JSObjectRef function,
              JSObjectRef this_object,
              size_t count,
              const JSValueRef arguments[],
              JSValueRef *exception)
{
    (void) function;
    (void) exception;
    if (count < 1) return JSValueMakeUndefined(context);
    ActionRuntime *runtime = JSObjectGetPrivate(this_object);
    char *id = js_string(context, arguments[0]);
    if (json_object_get_member(runtime->controls, id) == NULL ||
        get_element(runtime, id) == NULL) {
        char *message = g_strdup_printf("Unknown or unrendered control id: %s", id);
        set_exception(context, exception, message);
        g_free(message);
        g_free(id);
        return JSValueMakeUndefined(context);
    }
    char *value = get_value(runtime, id);
    g_free(id);
    if (value == NULL) return JSValueMakeNull(context);
    JSStringRef string = JSStringCreateWithUTF8CString(value);
    JSValueRef result = JSValueMakeString(context, string);
    JSStringRelease(string);
    g_free(value);
    return result;
}

static JSValueRef
dom_set_value(JSContextRef context,
              JSObjectRef function,
              JSObjectRef this_object,
              size_t count,
              const JSValueRef arguments[],
              JSValueRef *exception)
{
    (void) function;
    if (count < 2) {
        set_exception(context, exception, "setValue requires a control id and value.");
        return JSValueMakeUndefined(context);
    }
    ActionRuntime *runtime = JSObjectGetPrivate(this_object);
    char *id = js_string(context, arguments[0]);
    char *value = js_string(context, arguments[1]);
    if (json_object_get_member(runtime->controls, id) == NULL ||
        get_element(runtime, id) == NULL || !set_value(runtime, id, value)) {
        char *message = g_strdup_printf("Value is not supported by control '%s'.", id);
        set_exception(context, exception, message);
        g_free(message);
    }
    g_free(id);
    g_free(value);
    return JSValueMakeUndefined(context);
}

static JSValueRef
dom_get_checked(JSContextRef context,
                JSObjectRef function,
                JSObjectRef this_object,
                size_t count,
                const JSValueRef arguments[],
                JSValueRef *exception)
{
    (void) function;
    if (count < 1) return JSValueMakeUndefined(context);
    ActionRuntime *runtime = JSObjectGetPrivate(this_object);
    char *id = js_string(context, arguments[0]);
    GtkWidget *widget = get_element(runtime, id);
    if (widget == NULL) {
        char *message = g_strdup_printf("Unknown or unrendered control id: %s", id);
        set_exception(context, exception, message);
        g_free(message);
        g_free(id);
        return JSValueMakeUndefined(context);
    }
    g_free(id);
    if (!GTK_IS_CHECK_BUTTON(widget)) {
        set_exception(context, exception, "Control does not expose a checked state.");
        return JSValueMakeUndefined(context);
    }
    return JSValueMakeBoolean(context, gtk_check_button_get_active(GTK_CHECK_BUTTON(widget)));
}

static JSValueRef
dom_set_checked(JSContextRef context,
                JSObjectRef function,
                JSObjectRef this_object,
                size_t count,
                const JSValueRef arguments[],
                JSValueRef *exception)
{
    (void) function;
    if (count < 2) {
        set_exception(context, exception, "setChecked requires a control id and state.");
        return JSValueMakeUndefined(context);
    }
    ActionRuntime *runtime = JSObjectGetPrivate(this_object);
    char *id = js_string(context, arguments[0]);
    GtkWidget *widget = get_element(runtime, id);
    if (widget == NULL || !GTK_IS_CHECK_BUTTON(widget)) {
        set_exception(context, exception, "Control does not expose a checked state.");
        g_free(id);
        return JSValueMakeUndefined(context);
    }
    g_free(id);
    gtk_check_button_set_active(
        GTK_CHECK_BUTTON(widget), JSValueToBoolean(context, arguments[1]));
    return JSValueMakeUndefined(context);
}

static JSValueRef
dom_get_secure(JSContextRef context,
               JSObjectRef function,
               JSObjectRef this_object,
               size_t count,
               const JSValueRef arguments[],
               JSValueRef *exception)
{
    (void) function;
    if (count < 1) return JSValueMakeUndefined(context);
    ActionRuntime *runtime = JSObjectGetPrivate(this_object);
    char *id = js_string(context, arguments[0]);
    GtkWidget *root = get_element(runtime, id);
    if (root == NULL) {
        g_free(id);
        return JSValueMakeBoolean(context, false);
    }
    g_free(id);
    GtkWidget *entry = find_widget(root, GTK_TYPE_ENTRY);
    if (entry == NULL) {
        return JSValueMakeBoolean(context, false);
    }
    return JSValueMakeBoolean(context, !gtk_entry_get_visibility(GTK_ENTRY(entry)));
}

static JSValueRef
dom_set_secure(JSContextRef context,
               JSObjectRef function,
               JSObjectRef this_object,
               size_t count,
               const JSValueRef arguments[],
               JSValueRef *exception)
{
    (void) function;
    if (count < 2) {
        set_exception(context, exception, "setSecure requires a control id and boolean state.");
        return JSValueMakeUndefined(context);
    }
    ActionRuntime *runtime = JSObjectGetPrivate(this_object);
    char *id = js_string(context, arguments[0]);
    GtkWidget *root = get_element(runtime, id);
    if (root == NULL) {
        g_free(id);
        set_exception(context, exception, "Unknown control id.");
        return JSValueMakeUndefined(context);
    }
    g_free(id);
    GtkWidget *entry = find_widget(root, GTK_TYPE_ENTRY);
    if (entry == NULL) {
        set_exception(context, exception, "Control does not expose a secure state.");
        return JSValueMakeUndefined(context);
    }
    gboolean secure = JSValueToBoolean(context, arguments[1]);
    gtk_entry_set_visibility(GTK_ENTRY(entry), !secure);
    return JSValueMakeUndefined(context);
}

static JSValueRef
dom_set_status(JSContextRef context,
               JSObjectRef function,
               JSObjectRef this_object,
               size_t count,
               const JSValueRef arguments[],
               JSValueRef *exception)
{
    (void) function;
    (void) exception;
    if (count < 1) return JSValueMakeUndefined(context);
    ActionRuntime *runtime = JSObjectGetPrivate(this_object);
    char *text = js_string(context, arguments[0]);
    gtk_label_set_text(GTK_LABEL(runtime->status), text);
    g_free(text);
    return JSValueMakeUndefined(context);
}

static gboolean
control_is_rendered(ActionRuntime *runtime, const char *id)
{
    return json_object_get_member(runtime->controls, id) != NULL &&
        get_element(runtime, id) != NULL;
}

static void
unregister_tree(ActionRuntime *runtime, const char *id)
{
    JsonObject *spec = json_object_get_object_member(runtime->controls, id);
    if (spec != NULL) {
        const char *type = json_object_get_string_member(spec, "type");
        if (type != NULL &&
            (g_str_equal(type, "panel") || g_str_equal(type, "scrollPanel"))) {
            JsonArray *children = json_object_get_array_member(spec, "children");
            for (guint index = 0; index < json_array_get_length(children); index++) {
                unregister_tree(runtime, json_node_get_string(
                    json_array_get_element(children, index)));
            }
        }
    }
    g_hash_table_remove(runtime->elements, id);
}

static void
replace_json_options(JsonObject *spec, GPtrArray *items)
{
    JsonArray *array = json_array_new();
    for (guint index = 0; index < items->len; index++) {
        json_array_add_string_element(array, g_ptr_array_index(items, index));
    }
    json_object_set_array_member(spec, "options", array);
    json_array_unref(array);
}

static gboolean
read_option_list(JSContextRef context,
                 JSValueRef value,
                 GPtrArray **items,
                 JSValueRef *exception)
{
    if (!JSValueIsObject(context, value)) {
        set_exception(context, exception, "options must be an array of strings.");
        return FALSE;
    }
    JSObjectRef array = JSValueToObject(context, value, exception);
    JSStringRef length_name = JSStringCreateWithUTF8CString("length");
    if (!JSObjectHasProperty(context, array, length_name)) {
        JSStringRelease(length_name);
        set_exception(context, exception, "options must be an array of strings.");
        return FALSE;
    }
    JSValueRef length_value = JSObjectGetProperty(context, array, length_name, exception);
    JSStringRelease(length_name);
    double length_number = JSValueToNumber(context, length_value, exception);
    if (!(length_number >= 0) || length_number > 100000 ||
        length_number != (double) (int) length_number) {
        set_exception(context, exception, "options must be an array of strings.");
        return FALSE;
    }
    GPtrArray *parsed = g_ptr_array_new_with_free_func(g_free);
    for (int index = 0; index < (int) length_number; index++) {
        JSValueRef item = JSObjectGetPropertyAtIndex(context, array, (unsigned) index, exception);
        if (!JSValueIsString(context, item)) {
            g_ptr_array_free(parsed, TRUE);
            set_exception(context, exception, "options must be non-empty strings.");
            return FALSE;
        }
        char *text = js_string(context, item);
        char *trimmed = g_strstrip(g_strdup(text));
        gboolean blank = trimmed[0] == '\0';
        g_free(trimmed);
        if (blank) {
            g_free(text);
            g_ptr_array_free(parsed, TRUE);
            set_exception(context, exception, "options must be non-empty strings.");
            return FALSE;
        }
        g_ptr_array_add(parsed, text);
    }
    *items = parsed;
    return TRUE;
}

static gboolean
selection_index(ActionRuntime *runtime, const char *id, int *index)
{
    const char *type = control_type(runtime, id);
    GtkWidget *root = get_element(runtime, id);
    if (g_str_equal(type, "list")) {
        GtkWidget *list = find_widget(root, GTK_TYPE_LIST_BOX);
        if (list == NULL) return FALSE;
        GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(list));
        *index = row == NULL ? -1 : gtk_list_box_row_get_index(row);
        return TRUE;
    }
    if (g_str_equal(type, "comboBox")) {
        GtkWidget *combo = find_widget(root, GTK_TYPE_DROP_DOWN);
        if (combo == NULL) return FALSE;
        guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(combo));
        *index = selected == GTK_INVALID_LIST_POSITION ? -1 : (int) selected;
        return TRUE;
    }
    return FALSE;
}

static void
apply_options(ActionRuntime *runtime, const char *id, GPtrArray *items, int selected)
{
    const char *type = control_type(runtime, id);
    GtkWidget *root = get_element(runtime, id);
    JsonObject *spec = json_object_get_object_member(runtime->controls, id);
    replace_json_options(spec, items);
    if (g_str_equal(type, "list")) {
        GtkWidget *list = find_widget(root, GTK_TYPE_LIST_BOX);
        gtk_list_box_remove_all(GTK_LIST_BOX(list));
        for (guint index = 0; index < items->len; index++) {
            GtkWidget *row_label = gtk_label_new(g_ptr_array_index(items, index));
            gtk_widget_set_halign(row_label, GTK_ALIGN_START);
            gtk_list_box_append(GTK_LIST_BOX(list), row_label);
            const char *row_background = g_object_get_data(G_OBJECT(list), "omni-bg");
            const char *row_foreground = g_object_get_data(G_OBJECT(list), "omni-fg");
            GtkWidget *row = gtk_widget_get_parent(row_label);
            if (row != NULL) {
                control_renderer_style_widget(row, row_background, row_foreground);
            }
            control_renderer_style_widget(row_label, NULL, row_foreground);
        }
        if (selected >= 0) {
            gtk_list_box_select_row(GTK_LIST_BOX(list),
                gtk_list_box_get_row_at_index(GTK_LIST_BOX(list), selected));
        }
        return;
    }
    GtkWidget *combo = find_widget(root, GTK_TYPE_DROP_DOWN);
    GtkStringList *model = gtk_string_list_new(NULL);
    for (guint index = 0; index < items->len; index++) {
        gtk_string_list_append(model, g_ptr_array_index(items, index));
    }
    gtk_drop_down_set_model(GTK_DROP_DOWN(combo), G_LIST_MODEL(model));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(combo),
        selected < 0 ? GTK_INVALID_LIST_POSITION : (guint) selected);
    g_object_unref(model);
}

static JSValueRef
dom_add_panel(JSContextRef context,
              JSObjectRef function,
              JSObjectRef this_object,
              size_t count,
              const JSValueRef arguments[],
              JSValueRef *exception)
{
    (void) function;
    if (count < 1) {
        set_exception(context, exception, "addPanel requires a panel id.");
        return JSValueMakeUndefined(context);
    }
    ActionRuntime *runtime = JSObjectGetPrivate(this_object);
    char *id = js_string(context, arguments[0]);
    JsonNode *node = json_object_get_member(runtime->controls, id);
    if (node == NULL || !JSON_NODE_HOLDS_OBJECT(node)) {
        char *message = g_strdup_printf("Unknown control id: %s", id);
        set_exception(context, exception, message);
        g_free(message);
        g_free(id);
        return JSValueMakeUndefined(context);
    }
    const char *type = json_object_get_string_member(json_node_get_object(node), "type");
    if (type == NULL || !g_str_equal(type, "panel")) {
        char *message = g_strdup_printf("Control '%s' is not a panel.", id);
        set_exception(context, exception, message);
        g_free(message);
        g_free(id);
        return JSValueMakeUndefined(context);
    }
    if (get_element(runtime, id) != NULL) {
        char *message = g_strdup_printf("Panel '%s' is already shown.", id);
        set_exception(context, exception, message);
        g_free(message);
        g_free(id);
        return JSValueMakeUndefined(context);
    }
    if (runtime->container == NULL ||
        !control_renderer_render(id, json_node_get_object(node), runtime->controls,
                                 runtime->container, runtime->status, runtime)) {
        set_exception(context, exception, "The window is not ready to add a panel.");
        g_free(id);
        return JSValueMakeUndefined(context);
    }
    if (runtime->centered) {
        GtkWidget *rendered = gtk_widget_get_last_child(runtime->container);
        if (rendered != NULL) {
            gtk_widget_set_halign(rendered, GTK_ALIGN_CENTER);
            gtk_widget_set_valign(rendered, GTK_ALIGN_CENTER);
            gtk_widget_set_vexpand(rendered, TRUE);
        }
    }
    g_free(id);
    return JSValueMakeUndefined(context);
}

static JSValueRef
dom_remove_panel(JSContextRef context,
                 JSObjectRef function,
                 JSObjectRef this_object,
                 size_t count,
                 const JSValueRef arguments[],
                 JSValueRef *exception)
{
    (void) function;
    if (count < 1) {
        set_exception(context, exception, "removePanel requires a panel id.");
        return JSValueMakeUndefined(context);
    }
    ActionRuntime *runtime = JSObjectGetPrivate(this_object);
    char *id = js_string(context, arguments[0]);
    JsonNode *node = json_object_get_member(runtime->controls, id);
    if (node == NULL || !JSON_NODE_HOLDS_OBJECT(node)) {
        char *message = g_strdup_printf("Unknown control id: %s", id);
        set_exception(context, exception, message);
        g_free(message);
        g_free(id);
        return JSValueMakeUndefined(context);
    }
    const char *type = json_object_get_string_member(json_node_get_object(node), "type");
    if (type == NULL || !g_str_equal(type, "panel")) {
        char *message = g_strdup_printf("Control '%s' is not a panel.", id);
        set_exception(context, exception, message);
        g_free(message);
        g_free(id);
        return JSValueMakeUndefined(context);
    }
    GtkWidget *widget = get_element(runtime, id);
    if (widget == NULL || runtime->container == NULL) {
        char *message = g_strdup_printf("Panel '%s' is not shown.", id);
        set_exception(context, exception, message);
        g_free(message);
        g_free(id);
        return JSValueMakeUndefined(context);
    }
    unregister_tree(runtime, id);
    gtk_box_remove(GTK_BOX(runtime->container), widget);
    g_free(id);
    return JSValueMakeUndefined(context);
}

static JSValueRef
dom_set_options(JSContextRef context,
                JSObjectRef function,
                JSObjectRef this_object,
                size_t count,
                const JSValueRef arguments[],
                JSValueRef *exception)
{
    (void) function;
    if (count < 2) {
        set_exception(context, exception, "setOptions requires a control id and options.");
        return JSValueMakeUndefined(context);
    }
    ActionRuntime *runtime = JSObjectGetPrivate(this_object);
    char *id = js_string(context, arguments[0]);
    if (!control_is_rendered(runtime, id)) {
        char *message = g_strdup_printf("Unknown or unrendered control id: %s", id);
        set_exception(context, exception, message);
        g_free(message);
        g_free(id);
        return JSValueMakeUndefined(context);
    }
    const char *type = control_type(runtime, id);
    if (!g_str_equal(type, "list") && !g_str_equal(type, "comboBox")) {
        char *message = g_strdup_printf("Control '%s' does not expose options.", id);
        set_exception(context, exception, message);
        g_free(message);
        g_free(id);
        return JSValueMakeUndefined(context);
    }
    GPtrArray *items = NULL;
    if (!read_option_list(context, arguments[1], &items, exception)) {
        g_free(id);
        return JSValueMakeUndefined(context);
    }
    gboolean provided = count >= 3 && !JSValueIsUndefined(context, arguments[2]);
    int selected = 0;
    if (provided) {
        double number = JSValueToNumber(context, arguments[2], exception);
        if (!(number >= -1) || number > 100000 || number != (double) (int) number) {
            g_ptr_array_free(items, TRUE);
            g_free(id);
            set_exception(context, exception, "selectedIndex is out of range.");
            return JSValueMakeUndefined(context);
        }
        selected = (int) number;
    }
    if (items->len == 0) {
        if (provided && selected != -1) {
            g_ptr_array_free(items, TRUE);
            g_free(id);
            set_exception(context, exception, "selectedIndex is out of range.");
            return JSValueMakeUndefined(context);
        }
        selected = -1;
    } else if (selected < 0 || (guint) selected >= items->len) {
        g_ptr_array_free(items, TRUE);
        g_free(id);
        set_exception(context, exception, "selectedIndex is out of range.");
        return JSValueMakeUndefined(context);
    }
    apply_options(runtime, id, items, selected);
    g_ptr_array_free(items, TRUE);
    g_free(id);
    return JSValueMakeUndefined(context);
}

static JSValueRef
dom_get_selected_index(JSContextRef context,
                       JSObjectRef function,
                       JSObjectRef this_object,
                       size_t count,
                       const JSValueRef arguments[],
                       JSValueRef *exception)
{
    (void) function;
    if (count < 1) {
        set_exception(context, exception, "getSelectedIndex requires a control id.");
        return JSValueMakeUndefined(context);
    }
    ActionRuntime *runtime = JSObjectGetPrivate(this_object);
    char *id = js_string(context, arguments[0]);
    int index = -1;
    if (!control_is_rendered(runtime, id) || !selection_index(runtime, id, &index)) {
        char *message = g_strdup_printf(
            "Control '%s' does not expose a selection index.", id);
        set_exception(context, exception, message);
        g_free(message);
        g_free(id);
        return JSValueMakeUndefined(context);
    }
    g_free(id);
    return JSValueMakeNumber(context, index);
}

static void
set_property(JSContextRef context, JSObjectRef object, const char *name, JSValueRef value)
{
    JSStringRef property = JSStringCreateWithUTF8CString(name);
    JSObjectSetProperty(context, object, property, value,
                        kJSPropertyAttributeNone, NULL);
    JSStringRelease(property);
}

static JSObjectRef
new_event(ActionRuntime *runtime, const char *id, const char *event_type)
{
    JSContextRef context = runtime->context;
    JSObjectRef event = JSObjectMake(context, NULL, NULL);
    JSStringRef string = JSStringCreateWithUTF8CString(event_type);
    set_property(context, event, "type", JSValueMakeString(context, string));
    JSStringRelease(string);
    string = JSStringCreateWithUTF8CString(id);
    set_property(context, event, "targetId", JSValueMakeString(context, string));
    JSStringRelease(string);
    string = JSStringCreateWithUTF8CString(get_text(runtime, id));
    set_property(context, event, "text", JSValueMakeString(context, string));
    JSStringRelease(string);
    char *value = get_value(runtime, id);
    if (value == NULL) {
        set_property(context, event, "value", JSValueMakeNull(context));
    } else {
        string = JSStringCreateWithUTF8CString(value);
        set_property(context, event, "value", JSValueMakeString(context, string));
        JSStringRelease(string);
        g_free(value);
    }
    const char *type = control_type(runtime, id);
    if (g_str_equal(type, "checkbox")) {
        GtkWidget *widget = get_element(runtime, id);
        set_property(context, event, "checked", JSValueMakeBoolean(
            context, gtk_check_button_get_active(GTK_CHECK_BUTTON(widget))));
    } else {
        set_property(context, event, "checked", JSValueMakeNull(context));
    }
    return event;
}

static gboolean
evaluate_script(ActionRuntime *runtime,
                const char *file,
                gboolean action,
                GError **error)
{
    char *normalized = omni_resource_normalize(file, error);
    if (normalized == NULL) {
        return FALSE;
    }
    const char *extension = strrchr(normalized, '.');
    if (extension == NULL || g_ascii_strcasecmp(extension, ".js") != 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME,
                    action
                        ? "Action files must be relative .js paths without parent segments."
                        : "Script files must be relative .js paths without parent segments.");
        g_free(normalized);
        return FALSE;
    }
    if (!g_hash_table_contains(runtime->loaded_files, normalized)) {
        gsize length = 0;
        char *source = omni_resource_read(normalized, &length, error);
        if (source == NULL) {
            g_free(normalized);
            return FALSE;
        }
        JSStringRef script = JSStringCreateWithUTF8CString(source);
        JSStringRef source_url = JSStringCreateWithUTF8CString(normalized);
        JSValueRef exception = NULL;
        JSEvaluateScript(runtime->context, script, NULL, source_url, 1, &exception);
        JSStringRelease(script);
        JSStringRelease(source_url);
        g_free(source);
        if (exception != NULL) {
            char *message = js_string(runtime->context, exception);
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "Could not evaluate %s %s: %s",
                        action ? "action file" : "script file", file, message);
            g_free(message);
            g_free(normalized);
            return FALSE;
        }
        g_hash_table_add(runtime->loaded_files, g_strdup(normalized));
    }
    g_free(normalized);
    return TRUE;
}

static gboolean
load_action_file(ActionRuntime *runtime,
                 const char *file,
                 const char *function,
                 GError **error)
{
    if (!evaluate_script(runtime, file, TRUE, error)) {
        return FALSE;
    }
    JSStringRef function_name = JSStringCreateWithUTF8CString(function);
    JSValueRef value = JSObjectGetProperty(
        runtime->context, JSContextGetGlobalObject(runtime->context),
        function_name, NULL);
    JSStringRelease(function_name);
    if (value == NULL || !JSValueIsObject(runtime->context, value) ||
        !JSObjectIsFunction(runtime->context, JSValueToObject(runtime->context, value, NULL))) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                    "Action function '%s' was not defined by %s.", function, file);
        return FALSE;
    }
    return TRUE;
}

ActionRuntime *
action_runtime_new(JsonObject *controls,
                   JsonArray *scripts,
                   GtkWidget *status,
                   GError **error)
{
    ActionRuntime *runtime = g_new0(ActionRuntime, 1);
    runtime->context = JSGlobalContextCreate(NULL);
    runtime->controls = json_object_ref(controls);
    runtime->status = status;
    runtime->elements = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    runtime->loaded_files = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    JSClassDefinition definition = kJSClassDefinitionEmpty;
    definition.staticFunctions = dom_functions;
    definition.className = "OmniDOM";
    JSClassRef dom_class = JSClassCreate(&definition);
    runtime->dom = JSObjectMake(runtime->context, dom_class, runtime);
    JSStringRef dom_name = JSStringCreateWithUTF8CString("dom");
    JSObjectSetProperty(runtime->context,
        JSContextGetGlobalObject(runtime->context),
        dom_name, runtime->dom,
        kJSPropertyAttributeReadOnly, NULL);
    JSStringRelease(dom_name);
    JSClassRelease(dom_class);

    JSClassDefinition api_definition = kJSClassDefinitionEmpty;
    api_definition.className = "OmniAPI";
    api_definition.staticFunctions = api_functions;
    JSClassRef api_class = JSClassCreate(&api_definition);
    JSObjectRef api = JSObjectMake(runtime->context, api_class, runtime);
    JSStringRef api_name = JSStringCreateWithUTF8CString("api");
    JSObjectSetProperty(runtime->context,
        JSContextGetGlobalObject(runtime->context),
        api_name, api,
        kJSPropertyAttributeReadOnly, NULL);
    JSStringRelease(api_name);
    JSClassRelease(api_class);

    runtime->http = http_client_new(runtime->context);
    JSStringRef http_name = JSStringCreateWithUTF8CString("http");
    JSObjectSetProperty(runtime->context, api, http_name,
                        http_client_js_object(runtime->http),
                        kJSPropertyAttributeReadOnly, NULL);
    JSStringRelease(http_name);

    runtime->file = file_client_new(runtime->context, OMNI_PROJECT_NAME);
    JSStringRef file_name = JSStringCreateWithUTF8CString("file");
    JSObjectSetProperty(runtime->context, api, file_name,
                        file_client_js_object(runtime->file),
                        kJSPropertyAttributeReadOnly, NULL);
    JSStringRelease(file_name);

    runtime->crypto = crypto_client_new(runtime->context);
    JSObjectRef crypto_obj = crypto_client_js_object(runtime->crypto);
    JSStringRef crypto_name = JSStringCreateWithUTF8CString("crypto");
    JSObjectSetProperty(runtime->context, api, crypto_name, crypto_obj,
                        kJSPropertyAttributeReadOnly, NULL);
    JSStringRelease(crypto_name);

    JSStringRef enc_name = JSStringCreateWithUTF8CString("encrypt");
    JSValueRef enc_fn = JSObjectGetProperty(runtime->context, crypto_obj, enc_name, NULL);
    JSObjectSetProperty(runtime->context, api, enc_name, enc_fn,
                        kJSPropertyAttributeReadOnly, NULL);
    JSStringRelease(enc_name);

    JSStringRef dec_name = JSStringCreateWithUTF8CString("decrypt");
    JSValueRef dec_fn = JSObjectGetProperty(runtime->context, crypto_obj, dec_name, NULL);
    JSObjectSetProperty(runtime->context, api, dec_name, dec_fn,
                        kJSPropertyAttributeReadOnly, NULL);
    JSStringRelease(dec_name);

    if (scripts != NULL) {
        for (guint index = 0; index < json_array_get_length(scripts); index++) {
            const char *file = json_array_get_string_element(scripts, index);
            if (file == NULL || *file == '\0' ||
                !evaluate_script(runtime, file, FALSE, error)) {
                if (error != NULL && *error == NULL) {
                    g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                                "scripts must contain non-empty paths");
                }
                action_runtime_free(runtime);
                return NULL;
            }
        }
    }

    GList *ids = json_object_get_members(controls);
    for (GList *item = ids; item != NULL; item = item->next) {
        JsonObject *control = json_object_get_object_member(controls, item->data);
        if (!json_object_has_member(control, "actions")) continue;
        JsonObject *actions = json_object_get_object_member(control, "actions");
        GList *events = json_object_get_members(actions);
        for (GList *event = events; event != NULL; event = event->next) {
            JsonObject *action = json_object_get_object_member(actions, event->data);
            const char *file = json_object_get_string_member(action, "file");
            const char *function = json_object_get_string_member(action, "function");
            if (!load_action_file(runtime, file, function, error)) {
                g_list_free(events);
                g_list_free(ids);
                action_runtime_free(runtime);
                return NULL;
            }
        }
        g_list_free(events);
    }
    g_list_free(ids);
    return runtime;
}

void
action_runtime_register(ActionRuntime *runtime, const char *control_id, GtkWidget *widget)
{
    g_hash_table_replace(runtime->elements, g_strdup(control_id), widget);
}

void
action_runtime_invoke(ActionRuntime *runtime,
                      const char *control_id,
                      const char *event_type)
{
    if (runtime == NULL || runtime->invoking) return;
    JsonObject *control = json_object_get_object_member(runtime->controls, control_id);
    if (!json_object_has_member(control, "actions")) return;
    JsonObject *actions = json_object_get_object_member(control, "actions");
    if (!json_object_has_member(actions, event_type)) return;
    JsonObject *action = json_object_get_object_member(actions, event_type);
    const char *function = json_object_get_string_member(action, "function");
    JSStringRef name = JSStringCreateWithUTF8CString(function);
    JSValueRef value = JSObjectGetProperty(
        runtime->context, JSContextGetGlobalObject(runtime->context), name, NULL);
    JSStringRelease(name);
    if (value == NULL || !JSValueIsObject(runtime->context, value)) return;

    JSObjectRef handler = JSValueToObject(runtime->context, value, NULL);
    JSObjectRef event = new_event(runtime, control_id, event_type);
    JSValueRef arguments[] = { runtime->dom, event };
    JSValueRef exception = NULL;
    runtime->invoking = TRUE;
    JSObjectCallAsFunction(runtime->context, handler, NULL, 2, arguments, &exception);
    runtime->invoking = FALSE;
    if (exception != NULL) {
        char *message = js_string(runtime->context, exception);
        char *status = g_strdup_printf("JavaScript action failed: %s", message);
        gtk_label_set_text(GTK_LABEL(runtime->status), status);
        g_free(status);
        g_free(message);
    }
}

void
action_runtime_set_window(ActionRuntime *runtime, GtkWindow *window)
{
    if (runtime != NULL) {
        file_client_set_window(runtime->file, window);
    }
}

void
action_runtime_set_container(ActionRuntime *runtime,
                             GtkWidget *container,
                             gboolean centered)
{
    if (runtime == NULL) return;
    runtime->container = container;
    runtime->centered = centered;
}

void
action_runtime_free(gpointer data)
{
    ActionRuntime *runtime = data;
    if (runtime == NULL) return;
    crypto_client_free(runtime->crypto);
    file_client_free(runtime->file);
    http_client_free(runtime->http);
    JSGlobalContextRelease(runtime->context);
    json_object_unref(runtime->controls);
    g_hash_table_unref(runtime->elements);
    g_hash_table_unref(runtime->loaded_files);
    g_free(runtime);
}
