#include "control_renderer.h"

#include "ui_config.h"

#include <string.h>

typedef struct {
    GtkWidget *status;
    char *label;
    ActionRuntime *runtime;
    char *control_id;
    const char *event_type;
} StatusBinding;

typedef struct {
    GtkWidget *status;
    char *clicked_text;
    ActionRuntime *runtime;
    char *control_id;
} ButtonBinding;

typedef struct {
    StatusBinding *binding;
    char *option;
} RadioBinding;

static void
free_status_binding(gpointer user_data, GClosure *closure)
{
    (void) closure;
    StatusBinding *binding = user_data;
    g_free(binding->label);
    g_free(binding->control_id);
    g_free(binding);
}

static void
free_button_binding(gpointer user_data, GClosure *closure)
{
    (void) closure;
    ButtonBinding *binding = user_data;
    g_free(binding->clicked_text);
    g_free(binding->control_id);
    g_free(binding);
}

static void
set_status(StatusBinding *binding, const char *value)
{
    char *message = g_strdup_printf("%s: %s", binding->label, value);
    gtk_label_set_text(GTK_LABEL(binding->status), message);
    if (binding->event_type != NULL) {
        action_runtime_invoke(binding->runtime, binding->control_id,
                              binding->event_type);
    }
    g_free(message);
}

static void
on_button_clicked(GtkButton *button, gpointer user_data)
{
    ButtonBinding *binding = user_data;
    gtk_button_set_label(button, binding->clicked_text);
    gtk_label_set_text(GTK_LABEL(binding->status), binding->clicked_text);
    action_runtime_invoke(binding->runtime, binding->control_id, "click");
}

static void
on_combo_changed(GObject *object, GParamSpec *property, gpointer user_data)
{
    (void) property;
    StatusBinding *binding = user_data;
    GtkStringObject *selected =
        GTK_STRING_OBJECT(gtk_drop_down_get_selected_item(GTK_DROP_DOWN(object)));
    if (selected != NULL) {
        set_status(binding, gtk_string_object_get_string(selected));
    }
}

static void
on_entry_changed(GtkEditable *editable, gpointer user_data)
{
    if (GTK_IS_ENTRY(editable) && !gtk_entry_get_visibility(GTK_ENTRY(editable))) {
        StatusBinding *binding = user_data;
        char *message = g_strdup_printf("%s: hidden", binding->label);
        gtk_label_set_text(GTK_LABEL(binding->status), message);
        g_free(message);
        if (binding->event_type != NULL) {
            action_runtime_invoke(binding->runtime, binding->control_id,
                                  binding->event_type);
        }
        return;
    }
    set_status(user_data, gtk_editable_get_text(editable));
}

static void
on_text_buffer_changed(GtkTextBuffer *buffer, gpointer user_data)
{
    GtkTextIter start;
    GtkTextIter end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    char *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    set_status(user_data, text);
    g_free(text);
}

static void
on_checkbox_toggled(GtkCheckButton *button, gpointer user_data)
{
    StatusBinding *binding = user_data;
    set_status(binding, gtk_check_button_get_active(button) ? "on" : "off");
}

static void
free_radio_binding(gpointer user_data, GClosure *closure)
{
    (void) closure;
    RadioBinding *binding = user_data;
    free_status_binding(binding->binding, NULL);
    g_free(binding->option);
    g_free(binding);
}

static void
on_radio_toggled(GtkCheckButton *button, gpointer user_data)
{
    if (gtk_check_button_get_active(button)) {
        RadioBinding *binding = user_data;
        set_status(binding->binding, binding->option);
    }
}

static void
on_slider_changed(GtkRange *range, gpointer user_data)
{
    StatusBinding *binding = user_data;
    char *value = g_strdup_printf("%d", (int) gtk_range_get_value(range));
    set_status(binding, value);
    g_free(value);
}

static void
on_list_selected(GtkListBox *list_box, GtkListBoxRow *row, gpointer user_data)
{
    (void) list_box;
    if (row == NULL) {
        return;
    }
    StatusBinding *binding = user_data;
    GtkWidget *label = gtk_list_box_row_get_child(row);
    set_status(binding, gtk_label_get_text(GTK_LABEL(label)));
}

static StatusBinding *
new_status_binding(GtkWidget *status,
                   const char *label,
                   ActionRuntime *runtime,
                   const char *control_id,
                   const char *event_type)
{
    StatusBinding *binding = g_new0(StatusBinding, 1);
    binding->status = status;
    binding->label = g_strdup(label);
    binding->runtime = runtime;
    binding->control_id = g_strdup(control_id);
    binding->event_type = event_type;
    return binding;
}

static GtkWidget *
labeled_control(const char *label, GtkWidget *control)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *caption = gtk_label_new(label);
    gtk_widget_set_halign(caption, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), caption);
    gtk_box_append(GTK_BOX(box), control);
    return box;
}

static char *theme_background = NULL;
static char *theme_foreground = NULL;

/* Linearized sRGB channels, scaled by 1,000,000. */
static const unsigned linear_srgb[256] = {
    0, 304, 607, 911, 1214, 1518, 1821, 2125,
    2428, 2732, 3035, 3347, 3677, 4025, 4391, 4777,
    5182, 5605, 6049, 6512, 6995, 7499, 8023, 8568,
    9134, 9721, 10330, 10960, 11612, 12286, 12983, 13702,
    14444, 15209, 15996, 16807, 17642, 18500, 19382, 20289,
    21219, 22174, 23153, 24158, 25187, 26241, 27321, 28426,
    29557, 30713, 31896, 33105, 34340, 35601, 36889, 38204,
    39546, 40915, 42311, 43735, 45186, 46665, 48172, 49707,
    51269, 52861, 54480, 56128, 57805, 59511, 61246, 63010,
    64803, 66626, 68478, 70360, 72272, 74214, 76185, 78187,
    80220, 82283, 84376, 86500, 88656, 90842, 93059, 95307,
    97587, 99899, 102242, 104616, 107023, 109462, 111932, 114435,
    116971, 119538, 122139, 124772, 127438, 130136, 132868, 135633,
    138432, 141263, 144128, 147027, 149960, 152926, 155926, 158961,
    162029, 165132, 168269, 171441, 174647, 177888, 181164, 184475,
    187821, 191202, 194618, 198069, 201556, 205079, 208637, 212231,
    215861, 219526, 223228, 226966, 230740, 234551, 238398, 242281,
    246201, 250158, 254152, 258183, 262251, 266356, 270498, 274677,
    278894, 283149, 287441, 291771, 296138, 300544, 304987, 309469,
    313989, 318547, 323143, 327778, 332452, 337164, 341914, 346704,
    351533, 356400, 361307, 366253, 371238, 376262, 381326, 386429,
    391572, 396755, 401978, 407240, 412543, 417885, 423268, 428690,
    434154, 439657, 445201, 450786, 456411, 462077, 467784, 473531,
    479320, 485150, 491021, 496933, 502886, 508881, 514918, 520996,
    527115, 533276, 539479, 545724, 552011, 558340, 564712, 571125,
    577580, 584078, 590619, 597202, 603827, 610496, 617207, 623960,
    630757, 637597, 644480, 651406, 658375, 665387, 672443, 679542,
    686685, 693872, 701102, 708376, 715694, 723055, 730461, 737910,
    745404, 752942, 760525, 768151, 775822, 783538, 791298, 799103,
    806952, 814847, 822786, 830770, 838799, 846873, 854993, 863157,
    871367, 879622, 887923, 896269, 904661, 913099, 921582, 930111,
    938686, 947307, 955973, 964686, 973445, 982251, 991102, 1000000
};

void
control_renderer_set_theme(const char *background, const char *foreground)
{
    g_free(theme_background);
    g_free(theme_foreground);
    theme_background = g_strdup(background);
    theme_foreground = g_strdup(foreground);
}

const char *
control_renderer_contrast_foreground(const char *color)
{
    if (color == NULL || color[0] != '#' || strlen(color) != 7) {
        return "#000000";
    }
    unsigned channels[3];
    for (int index = 0; index < 3; index++) {
        int high = g_ascii_xdigit_value(color[1 + index * 2]);
        int low = g_ascii_xdigit_value(color[2 + index * 2]);
        if (high < 0 || low < 0) {
            return "#000000";
        }
        channels[index] = (unsigned) (high * 16 + low);
    }
    unsigned long luminance =
        (2126UL * linear_srgb[channels[0]] +
         7152UL * linear_srgb[channels[1]] +
         722UL * linear_srgb[channels[2]]) / 10000UL;
    return luminance > 179128UL ? "#000000" : "#FFFFFF";
}

void
control_renderer_style_widget(GtkWidget *widget,
                              const char *background,
                              const char *foreground)
{
    if (widget == NULL || (background == NULL && foreground == NULL)) {
        return;
    }
    static guint style_id;
    char *class_name = g_strdup_printf("allwhither-color-%u", ++style_id);
    GString *css = g_string_new("");
    g_string_append_printf(css, ".%s {", class_name);
    if (background != NULL) {
        g_string_append_printf(
            css, " background-color: %s; background-image: none;", background);
    }
    if (foreground != NULL) {
        g_string_append_printf(
            css, " color: %s; caret-color: %s;", foreground, foreground);
    }
    g_string_append(css, " }");
    if (background != NULL && foreground != NULL) {
        g_string_append_printf(
            css, " .%s:selected { outline: 2px solid %s; outline-offset: -2px; }",
            class_name, foreground);
    }
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css->str, -1);
    gtk_style_context_add_provider_for_display(
        gtk_widget_get_display(widget),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    gtk_widget_add_css_class(widget, class_name);
    g_object_unref(provider);
    g_string_free(css, TRUE);
    g_free(class_name);
}

static void
remember_list_colors(GtkWidget *widget, const char *background, const char *foreground)
{
    if (!GTK_IS_LIST_BOX(widget)) {
        return;
    }
    if (background != NULL) {
        g_object_set_data_full(G_OBJECT(widget), "allwhither-bg", g_strdup(background), g_free);
    }
    if (foreground != NULL) {
        g_object_set_data_full(G_OBJECT(widget), "allwhither-fg", g_strdup(foreground), g_free);
    }
}

static void
style_control_tree(GtkWidget *widget,
                   const char *background,
                   const char *foreground,
                   gboolean is_root)
{
    if (!is_root && g_object_get_data(G_OBJECT(widget), "allwhither-control-root") != NULL) {
        return;
    }
    if (GTK_IS_LIST_BOX_ROW(widget)) {
        control_renderer_style_widget(widget, background, foreground);
        for (GtkWidget *child = gtk_widget_get_first_child(widget);
             child != NULL;
             child = gtk_widget_get_next_sibling(child)) {
            control_renderer_style_widget(child, NULL, foreground);
        }
        return;
    }
    control_renderer_style_widget(widget, background, foreground);
    remember_list_colors(widget, background, foreground);
    for (GtkWidget *child = gtk_widget_get_first_child(widget);
         child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
        style_control_tree(child, background, foreground, FALSE);
    }
}

void
control_renderer_style_tree(GtkWidget *widget,
                            const char *background,
                            const char *foreground)
{
    if (widget == NULL) {
        return;
    }
    control_renderer_style_widget(widget, background, foreground);
    for (GtkWidget *child = gtk_widget_get_first_child(widget);
         child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
        control_renderer_style_tree(child, background, foreground);
    }
}

static void
resolve_control_colors(JsonObject *spec, const char **background, const char **foreground)
{
    const char *control_background = NULL;
    const char *control_foreground = NULL;
    ui_read_string(spec, "backgroundColor", &control_background);
    ui_read_string(spec, "foregroundColor", &control_foreground);
    *background = control_background != NULL ? control_background : theme_background;
    if (control_foreground != NULL) {
        *foreground = control_foreground;
    } else if (theme_foreground != NULL) {
        *foreground = theme_foreground;
    } else if (*background != NULL) {
        *foreground = control_renderer_contrast_foreground(*background);
    } else {
        *foreground = NULL;
    }
}

static GPtrArray *responsive_boxes = NULL;
static int responsive_width = 0;

typedef struct {
    GtkWidget *box;
    JsonObject *layout;
} ResponsiveBox;

static void
apply_layout(ResponsiveBox *entry)
{
    gboolean horizontal =
        g_str_equal(ui_layout_resolve(entry->layout, responsive_width), "horizontal");
    gtk_orientable_set_orientation(GTK_ORIENTABLE(entry->box),
        horizontal ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL);
    gtk_box_set_homogeneous(GTK_BOX(entry->box), horizontal);
    for (GtkWidget *child = gtk_widget_get_first_child(entry->box);
         child != NULL; child = gtk_widget_get_next_sibling(child)) {
        gboolean automatic =
            g_object_get_data(G_OBJECT(child), "allwhither-auto-hexpand") != NULL;
        if (horizontal && !gtk_widget_get_hexpand(child)) {
            gtk_widget_set_hexpand(child, TRUE);
            g_object_set_data(G_OBJECT(child), "allwhither-auto-hexpand", GINT_TO_POINTER(1));
        } else if (!horizontal && automatic) {
            gtk_widget_set_hexpand(child, FALSE);
            g_object_set_data(G_OBJECT(child), "allwhither-auto-hexpand", NULL);
        }
        if (horizontal || automatic) {
            gtk_widget_set_valign(child, horizontal ? GTK_ALIGN_START : GTK_ALIGN_FILL);
        }
    }
}

static void
free_responsive_box(gpointer data)
{
    ResponsiveBox *entry = data;
    json_object_unref(entry->layout);
    g_free(entry);
}

static void
on_responsive_box_destroyed(gpointer data, GObject *where_the_object_was)
{
    for (guint index = 0; index < responsive_boxes->len; index++) {
        ResponsiveBox *entry = g_ptr_array_index(responsive_boxes, index);
        if ((GObject *) entry->box == where_the_object_was) {
            g_ptr_array_remove_index_fast(responsive_boxes, index);
            return;
        }
    }
}

void
control_renderer_track_layout(GtkWidget *box, JsonObject *layout)
{
    if (responsive_boxes == NULL) {
        responsive_boxes = g_ptr_array_new_with_free_func(free_responsive_box);
    }
    ResponsiveBox *entry = g_new0(ResponsiveBox, 1);
    entry->box = box;
    entry->layout = json_object_ref(layout);
    g_ptr_array_add(responsive_boxes, entry);
    g_object_weak_ref(G_OBJECT(box), on_responsive_box_destroyed, NULL);
    apply_layout(entry);
}

void
control_renderer_update_layouts(int width)
{
    responsive_width = width;
    for (guint index = 0; responsive_boxes != NULL && index < responsive_boxes->len; index++) {
        apply_layout(g_ptr_array_index(responsive_boxes, index));
    }
}

static void
append_control(GtkWidget *parent,
               GtkWidget *control,
               JsonObject *spec,
               const char *control_id,
               ActionRuntime *runtime)
{
    const char *background = NULL;
    const char *foreground = NULL;
    resolve_control_colors(spec, &background, &foreground);
    g_object_set_data(G_OBJECT(control), "allwhither-control-root", GINT_TO_POINTER(1));
    if (background != NULL || foreground != NULL) {
        style_control_tree(control, background, foreground, TRUE);
    }
    action_runtime_register(runtime, control_id, control);
    gtk_box_append(GTK_BOX(parent), control);
}

gboolean
control_renderer_render(const char *control_id,
                        JsonObject *spec,
                        JsonObject *controls,
                        GtkWidget *parent,
                        GtkWidget *status,
                        ActionRuntime *runtime)
{
    const char *type;
    ui_read_string(spec, "type", &type);
    if (g_str_equal(type, "label")) {
        const char *text;
        ui_read_string(spec, "text", &text);
        GtkWidget *label = gtk_label_new(text);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        append_control(parent, label, spec, control_id, runtime);
        return TRUE;
    }
    if (g_str_equal(type, "button")) {
        const char *text;
        const char *clicked_text;
        ui_read_string(spec, "text", &text);
        ui_read_string(spec, "clickedText", &clicked_text);
        GtkWidget *button = gtk_button_new_with_label(text);
        ButtonBinding *binding = g_new0(ButtonBinding, 1);
        binding->status = status;
        binding->clicked_text = g_strdup(clicked_text);
        binding->runtime = runtime;
        binding->control_id = g_strdup(control_id);
        g_signal_connect_data(button, "clicked", G_CALLBACK(on_button_clicked),
                              binding, free_button_binding, 0);
        gtk_widget_set_halign(button, GTK_ALIGN_START);
        append_control(parent, button, spec, control_id, runtime);
        return TRUE;
    }
    if (g_str_equal(type, "comboBox")) {
        const char *label;
        int selected_index;
        JsonArray *items;
        ui_read_string(spec, "label", &label);
        ui_read_integer(spec, "selectedIndex", &selected_index);
        ui_read_array(spec, "options", &items);
        GtkStringList *options = gtk_string_list_new(NULL);
        for (guint index = 0; index < json_array_get_length(items); index++) {
            gtk_string_list_append(options,
                json_node_get_string(json_array_get_element(items, index)));
        }
        GtkWidget *combo = gtk_drop_down_new(G_LIST_MODEL(options), NULL);
        g_object_unref(options);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(combo), (guint) selected_index);
        StatusBinding *binding =
            new_status_binding(status, label, runtime, control_id, "change");
        g_signal_connect_data(combo, "notify::selected", G_CALLBACK(on_combo_changed),
                              binding, free_status_binding, 0);
        append_control(parent, labeled_control(label, combo), spec, control_id, runtime);
        return TRUE;
    }
    if (g_str_equal(type, "textField")) {
        const char *label;
        const char *value;
        ui_read_string(spec, "label", &label);
        ui_read_string_value(spec, "value", &value);
        const char *placeholder = "";
        if (json_object_has_member(spec, "placeholder")) {
            ui_read_string_value(spec, "placeholder", &placeholder);
        }
        gboolean password = FALSE;
        if (json_object_has_member(spec, "password")) {
            ui_read_boolean(spec, "password", &password);
        }
        GtkWidget *entry = gtk_entry_new();
        gtk_editable_set_text(GTK_EDITABLE(entry), value);
        gtk_entry_set_placeholder_text(GTK_ENTRY(entry), placeholder);
        gtk_entry_set_visibility(GTK_ENTRY(entry), !password);
        StatusBinding *binding =
            new_status_binding(status, label, runtime, control_id, "input");
        g_signal_connect_data(entry, "changed", G_CALLBACK(on_entry_changed),
                              binding, free_status_binding, 0);
        append_control(parent, labeled_control(label, entry), spec, control_id, runtime);
        return TRUE;
    }
    if (g_str_equal(type, "textArea")) {
        const char *label;
        const char *value;
        ui_read_string(spec, "label", &label);
        ui_read_string_value(spec, "value", &value);
        GtkWidget *text_view = gtk_text_view_new();
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
        gtk_text_buffer_set_text(
            gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view)), value, -1);
        GtkWidget *scroll = gtk_scrolled_window_new();
        gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 96);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                       GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), text_view);
        StatusBinding *binding =
            new_status_binding(status, label, runtime, control_id, "input");
        g_signal_connect_data(gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view)),
                              "changed", G_CALLBACK(on_text_buffer_changed),
                              binding, free_status_binding, 0);
        append_control(parent, labeled_control(label, scroll), spec, control_id, runtime);
        return TRUE;
    }
    if (g_str_equal(type, "checkbox")) {
        const char *label;
        gboolean checked;
        ui_read_string(spec, "label", &label);
        ui_read_boolean(spec, "checked", &checked);
        GtkWidget *checkbox = gtk_check_button_new_with_label(label);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(checkbox), checked);
        StatusBinding *binding =
            new_status_binding(status, label, runtime, control_id, "change");
        g_signal_connect_data(checkbox, "toggled", G_CALLBACK(on_checkbox_toggled),
                              binding, free_status_binding, 0);
        append_control(parent, checkbox, spec, control_id, runtime);
        return TRUE;
    }
    if (g_str_equal(type, "radioGroup")) {
        const char *label;
        int selected_index;
        JsonArray *options;
        ui_read_string(spec, "label", &label);
        ui_read_integer(spec, "selectedIndex", &selected_index);
        ui_read_array(spec, "options", &options);
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        GtkWidget *caption = gtk_label_new(label);
        gtk_widget_set_halign(caption, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(box), caption);
        GtkCheckButton *first = NULL;
        for (guint index = 0; index < json_array_get_length(options); index++) {
            const char *option =
                json_node_get_string(json_array_get_element(options, index));
            GtkWidget *radio = gtk_check_button_new_with_label(option);
            if (first == NULL) {
                first = GTK_CHECK_BUTTON(radio);
            } else {
                gtk_check_button_set_group(GTK_CHECK_BUTTON(radio), first);
            }
            if ((int) index == selected_index) {
                gtk_check_button_set_active(GTK_CHECK_BUTTON(radio), TRUE);
            }
            RadioBinding *binding = g_new0(RadioBinding, 1);
            binding->binding =
                new_status_binding(status, label, runtime, control_id, "change");
            binding->option = g_strdup(option);
            g_signal_connect_data(radio, "toggled", G_CALLBACK(on_radio_toggled),
                                  binding, free_radio_binding, 0);
            gtk_box_append(GTK_BOX(box), radio);
        }
        append_control(parent, box, spec, control_id, runtime);
        return TRUE;
    }
    if (g_str_equal(type, "slider")) {
        const char *label;
        int minimum;
        int maximum;
        int value;
        ui_read_string(spec, "label", &label);
        ui_read_integer(spec, "min", &minimum);
        ui_read_integer(spec, "max", &maximum);
        ui_read_integer(spec, "value", &value);
        GtkWidget *scale =
            gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, minimum, maximum, 1);
        gtk_scale_set_draw_value(GTK_SCALE(scale), TRUE);
        gtk_range_set_value(GTK_RANGE(scale), value);
        StatusBinding *binding =
            new_status_binding(status, label, runtime, control_id, "input");
        g_signal_connect_data(scale, "value-changed", G_CALLBACK(on_slider_changed),
                              binding, free_status_binding, 0);
        append_control(parent, labeled_control(label, scale), spec, control_id, runtime);
        return TRUE;
    }
    if (g_str_equal(type, "list")) {
        const char *label;
        int selected_index;
        JsonArray *items;
        ui_read_string(spec, "label", &label);
        ui_read_integer(spec, "selectedIndex", &selected_index);
        ui_read_array(spec, "options", &items);
        GtkWidget *list = gtk_list_box_new();
        gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
        for (guint index = 0; index < json_array_get_length(items); index++) {
            const char *text = json_node_get_string(json_array_get_element(items, index));
            GtkWidget *row_label = gtk_label_new(text);
            gtk_widget_set_halign(row_label, GTK_ALIGN_START);
            gtk_list_box_append(GTK_LIST_BOX(list), row_label);
            if ((int) index == selected_index) {
                gtk_list_box_select_row(GTK_LIST_BOX(list),
                    gtk_list_box_get_row_at_index(GTK_LIST_BOX(list), (int) index));
            }
        }
        GtkWidget *scroll = gtk_scrolled_window_new();
        gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 96);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                       GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
        StatusBinding *binding =
            new_status_binding(status, label, runtime, control_id, "change");
        g_signal_connect_data(list, "row-selected", G_CALLBACK(on_list_selected),
                              binding, free_status_binding, 0);
        append_control(parent, labeled_control(label, scroll), spec, control_id, runtime);
        return TRUE;
    }
    if (g_str_equal(type, "scrollPanel")) {
        const char *label;
        int height;
        JsonArray *children;
        ui_read_string(spec, "label", &label);
        ui_read_integer(spec, "height", &height);
        ui_read_array(spec, "children", &children);
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        GtkWidget *caption = gtk_label_new(label);
        gtk_widget_set_halign(caption, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(box), caption);
        GtkWidget *scroll = gtk_scrolled_window_new();
        gtk_widget_set_size_request(scroll, -1, height);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                       GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        GtkWidget *child_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        for (guint index = 0; index < json_array_get_length(children); index++) {
            const char *child_id =
                json_node_get_string(json_array_get_element(children, index));
            JsonNode *child_node = json_object_get_member(controls, child_id);
            if (!control_renderer_render(child_id, json_node_get_object(child_node),
                                         controls, child_box, status, runtime)) {
                return FALSE;
            }
        }
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), child_box);
        gtk_box_append(GTK_BOX(box), scroll);
        append_control(parent, box, spec, control_id, runtime);
        return TRUE;
    }
    if (g_str_equal(type, "group")) {
        JsonArray *children;
        ui_read_array(spec, "children", &children);
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        for (guint index = 0; index < json_array_get_length(children); index++) {
            const char *child_id =
                json_node_get_string(json_array_get_element(children, index));
            JsonNode *child_node = json_object_get_member(controls, child_id);
            if (!control_renderer_render(child_id, json_node_get_object(child_node),
                                         controls, box, status, runtime)) {
                return FALSE;
            }
        }
        if (json_object_has_member(spec, "layout")) {
            control_renderer_track_layout(
                box, json_object_get_object_member(spec, "layout"));
        }
        append_control(parent, box, spec, control_id, runtime);
        return TRUE;
    }
    if (g_str_equal(type, "panel")) {
        const char *label;
        JsonArray *children;
        ui_read_string(spec, "label", &label);
        ui_read_array(spec, "children", &children);
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_widget_set_hexpand(box, TRUE);
        gtk_widget_set_vexpand(box, TRUE);
        GtkWidget *caption = gtk_label_new(label);
        gtk_widget_add_css_class(caption, "title-3");
        gtk_widget_set_halign(caption, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(box), caption);
        for (guint index = 0; index < json_array_get_length(children); index++) {
            const char *child_id =
                json_node_get_string(json_array_get_element(children, index));
            JsonNode *child_node = json_object_get_member(controls, child_id);
            if (!control_renderer_render(child_id, json_node_get_object(child_node),
                                         controls, box, status, runtime)) {
                return FALSE;
            }
        }
        append_control(parent, box, spec, control_id, runtime);
        return TRUE;
    }
    return FALSE;
}
