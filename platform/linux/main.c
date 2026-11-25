#include "control_renderer.h"
#include "action_runtime.h"
#include "menu_renderer.h"
#include "ui_config.h"

#ifndef ALLWHITHER_PROJECT_NAME
#define ALLWHITHER_PROJECT_NAME "example"
#endif

static gboolean
on_window_tick(GtkWidget *window, GdkFrameClock *clock, gpointer user_data)
{
    (void)clock;
    (void)user_data;
    int width = gtk_widget_get_width(window);
    control_renderer_update_layouts(width);
    return G_SOURCE_CONTINUE;
}

static void
on_activate(GtkApplication *application, gpointer user_data)
{
    (void)user_data;
    const char *config_path = ALLWHITHER_PROJECT_NAME ".json";
    GError *error = NULL;
    JsonObject *root = ui_config_load(config_path, &error);
    if (root == NULL) {
        g_printerr("Could not load %s: %s\n", config_path, error->message);
        g_clear_error(&error);
        g_application_quit(G_APPLICATION(application));
        return;
    }

    const char *title;
    int width;
    int height;
    JsonArray *menus;
    JsonObject *controls;
    JsonArray *order;
    if (!ui_config_validate(root, &title, &width, &height, &menus, &controls, &order)) {
        g_printerr("Invalid UI definition in %s\n", config_path);
        json_object_unref(root);
        g_application_quit(G_APPLICATION(application));
        return;
    }

    GtkWidget *window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(window), title);
    gtk_window_set_default_size(GTK_WINDOW(window), width, height);
    JsonObject *ui = json_object_get_object_member(root, "ui");
    JsonObject *window_definition = json_object_get_object_member(ui, "window");
    JsonObject *layout = json_object_get_object_member(window_definition, "layout");
    const char *layout_type = json_object_get_string_member(layout, "type");
    gboolean fullscreen = FALSE;
    if (json_object_has_member(window_definition, "fullscreen")) {
        ui_read_boolean(window_definition, "fullscreen", &fullscreen);
    }
    gboolean fullscreen_center = fullscreen && g_str_equal(layout_type, "center");
    const char *theme_background = NULL;
    const char *theme_foreground = NULL;
    if (json_object_has_member(ui, "theme") &&
        JSON_NODE_HOLDS_OBJECT(json_object_get_member(ui, "theme"))) {
        JsonObject *theme = json_object_get_object_member(ui, "theme");
        ui_read_string(theme, "backgroundColor", &theme_background);
        ui_read_string(theme, "foregroundColor", &theme_foreground);
    }
    control_renderer_set_theme(theme_background, theme_foreground);
    const char *window_background = NULL;
    const char *window_foreground = NULL;
    ui_read_string(window_definition, "backgroundColor", &window_background);
    ui_read_string(window_definition, "foregroundColor", &window_foreground);
    if (window_foreground == NULL && window_background != NULL) {
        window_foreground = control_renderer_contrast_foreground(window_background);
    }
    if (fullscreen) {
        gtk_window_fullscreen(GTK_WINDOW(window));
    }
    GtkWidget *root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    GtkWidget *status = gtk_label_new("Ready");
    gtk_widget_set_halign(status, GTK_ALIGN_START);
    JsonArray *scripts = NULL;
    if (json_object_has_member(root, "scripts") &&
        JSON_NODE_HOLDS_ARRAY(json_object_get_member(root, "scripts"))) {
        scripts = json_node_get_array(json_object_get_member(root, "scripts"));
    }
    ActionRuntime *runtime = action_runtime_new(controls, scripts, status, &error);
    if (runtime == NULL) {
        g_printerr("Could not load JavaScript actions: %s\n", error->message);
        g_clear_error(&error);
        json_object_unref(root);
        g_application_quit(G_APPLICATION(application));
        return;
    }
    g_object_set_data_full(G_OBJECT(window), "allwhither-action-runtime",
                           runtime, action_runtime_free);
    action_runtime_set_window(runtime, GTK_WINDOW(window));
    GtkWidget *menu_bar = NULL;
    if (!fullscreen_center && json_array_get_length(menus) > 0) {
        menu_bar = menu_renderer_create(application, menus, status);
        gtk_box_append(GTK_BOX(root_box), menu_bar);
    }
    if (window_background != NULL || window_foreground != NULL) {
        control_renderer_style_widget(window, window_background, window_foreground);
        control_renderer_style_widget(root_box, window_background, window_foreground);
        control_renderer_style_widget(status, NULL, window_foreground);
        if (menu_bar != NULL) {
            control_renderer_style_tree(menu_bar, window_background, window_foreground);
        }
    }

    GtkWidget *control_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gboolean centered_layout = g_str_equal(layout_type, "center");
    if (centered_layout) {
        gtk_widget_set_vexpand(control_box, TRUE);
    }
    action_runtime_set_container(runtime, control_box, centered_layout);
    control_renderer_update_layouts(width);
    if (!centered_layout) {
        control_renderer_track_layout(control_box, layout);
    }
    for (guint index = 0; index < json_array_get_length(order); index++) {
        const char *id = json_node_get_string(json_array_get_element(order, index));
        JsonNode *control = json_object_get_member(controls, id);
        if (control_renderer_render(id, json_node_get_object(control), controls,
                                    control_box, status, runtime) && centered_layout) {
            GtkWidget *rendered = gtk_widget_get_last_child(control_box);
            gtk_widget_set_halign(rendered, GTK_ALIGN_CENTER);
            gtk_widget_set_valign(rendered, GTK_ALIGN_CENTER);
            gtk_widget_set_vexpand(
                rendered, !json_object_has_member(
                    json_node_get_object(control), "height"));
        }
    }
    if (fullscreen_center) {
        gtk_box_append(GTK_BOX(root_box), control_box);
    } else {
        GtkWidget *main_scroll = gtk_scrolled_window_new();
        gtk_widget_set_vexpand(main_scroll, TRUE);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(main_scroll),
                                       GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(main_scroll), control_box);
        gtk_box_append(GTK_BOX(root_box), main_scroll);
        gtk_box_append(GTK_BOX(root_box), status);
    }
    gtk_window_set_child(GTK_WINDOW(window), root_box);
    control_renderer_update_layouts(width);
    gtk_widget_add_tick_callback(window, on_window_tick, NULL, NULL);
    gtk_window_present(GTK_WINDOW(window));
    json_object_unref(root);
}

int
main(int argc, char **argv)
{
    (void)argc;
    GtkApplication *application =
        gtk_application_new("org.allwhither.starter", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "activate", G_CALLBACK(on_activate), NULL);
    int status = g_application_run(G_APPLICATION(application), 1, argv);
    g_object_unref(application);
    return status;
}
