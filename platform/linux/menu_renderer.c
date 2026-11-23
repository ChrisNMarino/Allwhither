#include "menu_renderer.h"

typedef struct {
    GtkWidget *status;
    char *message;
} MenuBinding;

static void
free_menu_binding(gpointer user_data, GClosure *closure)
{
    (void) closure;
    MenuBinding *binding = user_data;
    g_free(binding->message);
    g_free(binding);
}

static void
on_menu_activated(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void) action;
    (void) parameter;
    MenuBinding *binding = user_data;
    gtk_label_set_text(GTK_LABEL(binding->status), binding->message);
}

GtkWidget *
menu_renderer_create(GtkApplication *application, JsonArray *menus, GtkWidget *status)
{
    GMenu *menu_bar = g_menu_new();
    for (guint menu_index = 0; menu_index < json_array_get_length(menus); menu_index++) {
        JsonObject *menu = json_node_get_object(json_array_get_element(menus, menu_index));
        const char *label = json_object_get_string_member(menu, "label");
        JsonArray *items = json_object_get_array_member(menu, "items");
        GMenu *submenu = g_menu_new();
        for (guint item_index = 0; item_index < json_array_get_length(items); item_index++) {
            JsonObject *item = json_node_get_object(json_array_get_element(items, item_index));
            const char *text = json_object_get_string_member(item, "text");
            const char *message = json_object_get_string_member(item, "message");
            char *action_name = g_strdup_printf("menu-%u-%u", menu_index, item_index);
            char *detailed_action = g_strdup_printf("app.%s", action_name);
            GSimpleAction *action = g_simple_action_new(action_name, NULL);
            MenuBinding *binding = g_new0(MenuBinding, 1);
            binding->status = status;
            binding->message = g_strdup(message);
            g_signal_connect_data(action, "activate", G_CALLBACK(on_menu_activated),
                                  binding, free_menu_binding, 0);
            g_action_map_add_action(G_ACTION_MAP(application), G_ACTION(action));

            GMenuItem *menu_item = g_menu_item_new(text, detailed_action);
            g_menu_append_item(submenu, menu_item);
            g_object_unref(menu_item);
            g_object_unref(action);
            g_free(action_name);
            g_free(detailed_action);
        }
        GMenuItem *top_level = g_menu_item_new(label, NULL);
        g_menu_item_set_submenu(top_level, G_MENU_MODEL(submenu));
        g_menu_append_item(menu_bar, top_level);
        g_object_unref(top_level);
        g_object_unref(submenu);
    }
    GtkWidget *widget = gtk_popover_menu_bar_new_from_model(G_MENU_MODEL(menu_bar));
    g_object_unref(menu_bar);
    return widget;
}
