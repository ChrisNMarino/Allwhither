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
    ActionRuntime *runtime;
    char *control_id;
    char *event_type;
} DeferredAction;

typedef struct {
    StatusBinding *binding;
    char *option;
} RadioBinding;

/* GTK size requests are minimums, so maxWidth needs a container that caps allocation. */
typedef struct {
    GtkWidget parent_instance;
    GtkWidget *child;
    int max_width;
    gboolean centered;
} MaxWidthContainer;

typedef struct {
    GtkWidgetClass parent_class;
} MaxWidthContainerClass;

G_DEFINE_TYPE(MaxWidthContainer, max_width_container, GTK_TYPE_WIDGET)

static int
max_width_container_total_width(MaxWidthContainer *container)
{
    return container->max_width +
        gtk_widget_get_margin_start(container->child) +
        gtk_widget_get_margin_end(container->child);
}

static void
max_width_container_measure(GtkWidget *widget,
                            GtkOrientation orientation,
                            int for_size,
                            int *minimum,
                            int *natural,
                            int *minimum_baseline,
                            int *natural_baseline)
{
    MaxWidthContainer *container = (MaxWidthContainer *) widget;
    int child_for_size = for_size;
    if (orientation == GTK_ORIENTATION_VERTICAL && for_size >= 0) {
        child_for_size = MIN(for_size, max_width_container_total_width(container));
    }
    gtk_widget_measure(container->child, orientation, child_for_size,
                       minimum, natural, minimum_baseline, natural_baseline);
    if (orientation == GTK_ORIENTATION_HORIZONTAL) {
        int limit = max_width_container_total_width(container);
        if (minimum != NULL) *minimum = MIN(*minimum, limit);
        if (natural != NULL) *natural = MIN(*natural, limit);
    }
}

static GtkSizeRequestMode
max_width_container_get_request_mode(GtkWidget *widget)
{
    MaxWidthContainer *container = (MaxWidthContainer *) widget;
    return gtk_widget_get_request_mode(container->child);
}

static void
max_width_container_size_allocate(GtkWidget *widget,
                                  int width,
                                  int height,
                                  int baseline)
{
    MaxWidthContainer *container = (MaxWidthContainer *) widget;
    int child_width = MIN(width, max_width_container_total_width(container));
    int x = container->centered ? (width - child_width) / 2 : 0;
    graphene_point_t point = GRAPHENE_POINT_INIT(x, 0);
    GskTransform *transform = gsk_transform_translate(NULL, &point);
    gtk_widget_allocate(container->child, child_width, height, baseline, transform);
}

static void
max_width_container_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
    MaxWidthContainer *container = (MaxWidthContainer *) widget;
    gtk_widget_snapshot_child(widget, container->child, snapshot);
}

static void
max_width_container_dispose(GObject *object)
{
    MaxWidthContainer *container = (MaxWidthContainer *) object;
    if (container->child != NULL) {
        gtk_widget_unparent(container->child);
        container->child = NULL;
    }
    G_OBJECT_CLASS(max_width_container_parent_class)->dispose(object);
}

static void
max_width_container_class_init(MaxWidthContainerClass *class)
{
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(class);
    G_OBJECT_CLASS(class)->dispose = max_width_container_dispose;
    widget_class->get_request_mode = max_width_container_get_request_mode;
    widget_class->measure = max_width_container_measure;
    widget_class->size_allocate = max_width_container_size_allocate;
    widget_class->snapshot = max_width_container_snapshot;
    gtk_widget_class_set_css_name(widget_class, "maxwidthcontainer");
}

static void
max_width_container_init(MaxWidthContainer *container)
{
    (void) container;
}

static GtkWidget *
max_width_container_new(GtkWidget *child, int max_width, gboolean centered)
{
    MaxWidthContainer *container =
        g_object_new(max_width_container_get_type(), NULL);
    container->child = child;
    container->max_width = max_width;
    container->centered = centered;
    gtk_widget_set_parent(child, GTK_WIDGET(container));
    return GTK_WIDGET(container);
}

static void
max_width_container_set_max_width(GtkWidget *widget, int max_width)
{
    MaxWidthContainer *container = (MaxWidthContainer *) widget;
    if (container->max_width != max_width) {
        container->max_width = max_width;
        gtk_widget_queue_resize(widget);
    }
}

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

static void defer_action(ActionRuntime *runtime,
                         const char *control_id,
                         const char *event_type);

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
    defer_action(binding->runtime, binding->control_id, "click");
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

static gboolean
invoke_deferred_action(gpointer user_data)
{
    DeferredAction *action = user_data;
    action_runtime_invoke(action->runtime, action->control_id, action->event_type);
    return G_SOURCE_REMOVE;
}

static void
free_deferred_action(gpointer user_data)
{
    DeferredAction *action = user_data;
    g_free(action->control_id);
    g_free(action->event_type);
    g_free(action);
}

static void
defer_action(ActionRuntime *runtime, const char *control_id, const char *event_type)
{
    DeferredAction *action = g_new0(DeferredAction, 1);
    action->runtime = runtime;
    action->control_id = g_strdup(control_id);
    action->event_type = g_strdup(event_type);
    g_idle_add_full(G_PRIORITY_DEFAULT, invoke_deferred_action,
                    action, free_deferred_action);
}

static void
on_entry_activated(GtkEntry *entry, gpointer user_data)
{
    (void) entry;
    StatusBinding *binding = user_data;
    defer_action(binding->runtime, binding->control_id, "enter");
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
style_border(GtkWidget *widget, const char *style, int width, const char *color)
{
    if (widget == NULL || style == NULL || color == NULL ||
        g_str_equal(style, "none")) {
        return;
    }
    static guint border_id;
    char *class_name = g_strdup_printf("allwhither-border-%u", ++border_id);
    char *css = g_strdup_printf(".%s { border: %dpx %s %s; }",
                                class_name, width, style, color);
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, css);
    gtk_style_context_add_provider_for_display(
        gtk_widget_get_display(widget),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    gtk_widget_add_css_class(widget, class_name);
    g_object_unref(provider);
    g_free(css);
    g_free(class_name);
}

static void
style_font(GtkWidget *widget, int font_size)
{
    static guint font_id;
    char *class_name = g_strdup_printf("allwhither-font-%u", ++font_id);
    char *css = g_strdup_printf(".%s { font-size: %dpx; }", class_name, font_size);
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, css);
    gtk_style_context_add_provider_for_display(
        gtk_widget_get_display(widget),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    gtk_widget_add_css_class(widget, class_name);
    g_object_unref(provider);
    g_free(css);
    g_free(class_name);
}

static void
style_font_tree(GtkWidget *widget, int font_size, gboolean is_root)
{
    if (!is_root &&
        g_object_get_data(G_OBJECT(widget), "allwhither-control-root") != NULL) {
        return;
    }
    style_font(widget, font_size);
    for (GtkWidget *child = gtk_widget_get_first_child(widget);
         child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
        style_font_tree(child, font_size, FALSE);
    }
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
static GPtrArray *responsive_sizes = NULL;
static GPtrArray *responsive_visibilities = NULL;
static int responsive_width = 0;

typedef struct {
    GtkWidget *box;
    JsonObject *layout;
} ResponsiveBox;

typedef struct {
    GWeakRef control;
    JsonArray *media;
} ResponsiveVisibility;

typedef struct {
    GWeakRef control;
    GWeakRef max_width_container;
    GWeakRef parent;
    int width;
    int height;
    double width_percent;
    double height_percent;
    gboolean has_width;
    gboolean has_height;
    gboolean has_max_width;
    int max_width;
    int horizontal_margin;
    gboolean percent_width;
    gboolean percent_height;
    int last_parent_width;
    int last_parent_height;
} ResponsiveSize;

static void
free_responsive_size(gpointer data)
{
    ResponsiveSize *entry = data;
    g_weak_ref_clear(&entry->control);
    g_weak_ref_clear(&entry->max_width_container);
    g_weak_ref_clear(&entry->parent);
    g_free(entry);
}

static void
apply_layout(ResponsiveBox *entry)
{
    gboolean horizontal =
        g_str_equal(ui_layout_resolve(entry->layout, responsive_width), "horizontal");
    gtk_orientable_set_orientation(GTK_ORIENTABLE(entry->box),
        horizontal ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL);
    gboolean has_fixed_width_child = FALSE;
    for (GtkWidget *child = gtk_widget_get_first_child(entry->box);
         child != NULL; child = gtk_widget_get_next_sibling(child)) {
        has_fixed_width_child |= gtk_widget_get_visible(child) &&
            g_object_get_data(G_OBJECT(child), "allwhither-fixed-width") != NULL;
    }
    gtk_box_set_homogeneous(GTK_BOX(entry->box),
                            horizontal && !has_fixed_width_child);
    for (GtkWidget *child = gtk_widget_get_first_child(entry->box);
         child != NULL; child = gtk_widget_get_next_sibling(child)) {
        gboolean automatic =
            g_object_get_data(G_OBJECT(child), "allwhither-auto-hexpand") != NULL;
        gboolean fixed_width =
            g_object_get_data(G_OBJECT(child), "allwhither-fixed-width") != NULL;
        if (horizontal && !fixed_width && !gtk_widget_get_hexpand(child)) {
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
free_responsive_visibility(gpointer data)
{
    ResponsiveVisibility *entry = data;
    g_weak_ref_clear(&entry->control);
    json_array_unref(entry->media);
    g_free(entry);
}

static void
track_responsive_visibility(GtkWidget *control, JsonArray *media)
{
    if (responsive_visibilities == NULL) {
        responsive_visibilities = g_ptr_array_new_with_free_func(
            free_responsive_visibility);
    }
    ResponsiveVisibility *entry = g_new0(ResponsiveVisibility, 1);
    g_weak_ref_init(&entry->control, G_OBJECT(control));
    entry->media = json_array_ref(media);
    g_ptr_array_add(responsive_visibilities, entry);
    gtk_widget_set_visible(control,
        ui_control_media_visible(media, responsive_width));
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
    if (responsive_width != width) {
        responsive_width = width;
        for (guint index = 0;
             responsive_boxes != NULL && index < responsive_boxes->len; index++) {
            apply_layout(g_ptr_array_index(responsive_boxes, index));
        }
    }
    for (guint index = 0;
         responsive_visibilities != NULL && index < responsive_visibilities->len;) {
        ResponsiveVisibility *entry =
            g_ptr_array_index(responsive_visibilities, index);
        GtkWidget *control = g_weak_ref_get(&entry->control);
        if (control == NULL) {
            g_ptr_array_remove_index_fast(responsive_visibilities, index);
            continue;
        }
        gtk_widget_set_visible(control,
            ui_control_media_visible(entry->media, responsive_width));
        g_object_unref(control);
        index++;
    }
    for (guint index = 0; responsive_sizes != NULL && index < responsive_sizes->len;) {
        ResponsiveSize *entry = g_ptr_array_index(responsive_sizes, index);
        GtkWidget *control = g_weak_ref_get(&entry->control);
        GtkWidget *max_width_container = entry->has_max_width
            ? g_weak_ref_get(&entry->max_width_container) : NULL;
        GtkWidget *parent = g_weak_ref_get(&entry->parent);
        if (control == NULL || parent == NULL ||
            (entry->has_max_width && max_width_container == NULL)) {
            if (control != NULL) g_object_unref(control);
            if (max_width_container != NULL) g_object_unref(max_width_container);
            if (parent != NULL) g_object_unref(parent);
            g_ptr_array_remove_index_fast(responsive_sizes, index);
            continue;
        }
        int parent_width = gtk_widget_get_width(parent);
        int parent_height = gtk_widget_get_height(parent);
        gboolean dimensions_ready =
            (!entry->percent_width || parent_width > 0) &&
            (!entry->percent_height || parent_height > 0) &&
            (!entry->has_max_width || parent_width > 0);
        if (dimensions_ready &&
            (parent_width != entry->last_parent_width ||
             parent_height != entry->last_parent_height)) {
            int requested_width = entry->has_width ? entry->width : -1;
            int requested_height = entry->has_height ? entry->height : -1;
            if (entry->percent_width) {
                requested_width = (int) (parent_width * entry->width_percent / 100.0 + 0.5);
            }
            if (entry->percent_height) {
                requested_height = (int) (parent_height * entry->height_percent / 100.0 + 0.5);
            }
            if (entry->has_max_width) {
                int available_width = MAX(1, parent_width -
                    entry->horizontal_margin);
                int width_limit = MIN(entry->max_width, available_width);
                max_width_container_set_max_width(max_width_container, width_limit);
                if (requested_width > width_limit) {
                    requested_width = width_limit;
                }
            }
            gtk_widget_set_size_request(control, requested_width, requested_height);
            entry->last_parent_width = parent_width;
            entry->last_parent_height = parent_height;
        }
        g_object_unref(control);
        if (max_width_container != NULL) g_object_unref(max_width_container);
        g_object_unref(parent);
        index++;
    }
}

static void
track_responsive_size(GtkWidget *control,
                      GtkWidget *parent,
                      gboolean has_width,
                      int width,
                      gboolean percent_width,
                      double width_percent,
                      gboolean has_height,
                      int height,
                      gboolean percent_height,
                      double height_percent,
                      gboolean has_max_width,
                      int max_width,
                      GtkWidget *max_width_widget)
{
    if (responsive_sizes == NULL) {
        responsive_sizes = g_ptr_array_new_with_free_func(free_responsive_size);
    }
    ResponsiveSize *entry = g_new0(ResponsiveSize, 1);
    g_weak_ref_init(&entry->control, G_OBJECT(control));
    g_weak_ref_init(&entry->max_width_container,
                    max_width_widget == NULL ? NULL : G_OBJECT(max_width_widget));
    g_weak_ref_init(&entry->parent, G_OBJECT(parent));
    entry->width = width;
    entry->height = height;
    entry->width_percent = width_percent;
    entry->height_percent = height_percent;
    entry->has_width = has_width;
    entry->has_height = has_height;
    entry->has_max_width = has_max_width;
    entry->max_width = max_width;
    entry->horizontal_margin = gtk_widget_get_margin_start(control) +
        gtk_widget_get_margin_end(control);
    entry->percent_width = percent_width;
    entry->percent_height = percent_height;
    g_ptr_array_add(responsive_sizes, entry);
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
    gboolean has_width = json_object_has_member(spec, "width");
    gboolean has_height =
        json_object_has_member(spec, "height") &&
        !g_str_equal(json_object_get_string_member(spec, "type"), "scrollPanel");
    gboolean has_max_width = json_object_has_member(spec, "maxWidth");
    int max_width = 0;
    if (has_max_width) {
        ui_read_integer(spec, "maxWidth", &max_width);
    }
    gboolean width_is_percent = FALSE;
    gboolean height_is_percent = FALSE;
    double width_value = 0;
    double height_value = 0;
    if (has_width) {
        ui_read_dimension(spec, "width", &width_is_percent, &width_value);
    }
    if (has_height) {
        ui_read_dimension(spec, "height", &height_is_percent, &height_value);
    }
    int requested_width = has_width && !width_is_percent ? (int) width_value : -1;
    int requested_height = has_height && !height_is_percent ? (int) height_value : -1;
    if (has_max_width && requested_width > max_width) {
        requested_width = max_width;
    }
    if (has_width || has_height) {
        gtk_widget_set_size_request(control, requested_width, requested_height);
    }
    if (has_width) {
        gtk_widget_set_hexpand(control, FALSE);
        gtk_widget_set_halign(control,
            json_object_has_member(spec, "center") &&
            json_object_get_boolean_member(spec, "center")
                ? GTK_ALIGN_CENTER : GTK_ALIGN_START);
    }
    if (has_height) {
        gtk_widget_set_vexpand(control, FALSE);
        gtk_widget_set_valign(control, GTK_ALIGN_START);
    }
    gboolean centered = FALSE;
    if (json_object_has_member(spec, "center")) {
        ui_read_boolean(spec, "center", &centered);
    }
    if (centered) {
        gtk_widget_set_hexpand(control, FALSE);
        gtk_widget_set_halign(control, GTK_ALIGN_CENTER);
    }
    if (has_max_width) {
        gtk_widget_set_hexpand(control, TRUE);
        gtk_widget_set_halign(control, GTK_ALIGN_FILL);
    }
    if (json_object_has_member(spec, "margin")) {
        int margin;
        if (ui_read_integer(spec, "margin", &margin) && margin >= 0) {
            gtk_widget_set_margin_top(control, margin);
            gtk_widget_set_margin_bottom(control, margin);
            gtk_widget_set_margin_start(control, margin);
            gtk_widget_set_margin_end(control, margin);
        }
    }
    if (json_object_has_member(spec, "padding")) {
        int padding;
        if (ui_read_integer(spec, "padding", &padding) && padding >= 0) {
            GtkCssProvider *provider = gtk_css_provider_new();
            char *css = g_strdup_printf("* { padding: %dpx; }", padding);
            gtk_css_provider_load_from_string(provider, css);
            gtk_style_context_add_provider(
                gtk_widget_get_style_context(control),
                GTK_STYLE_PROVIDER(provider),
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
            g_free(css);
            g_object_unref(provider);
        }
    }
    if (json_object_has_member(spec, "borderStyle") ||
        json_object_has_member(spec, "borderColor") ||
        json_object_has_member(spec, "borderWidth")) {
        const char *style = "solid";
        const char *color = "#000000";
        int border_width = 1;
        ui_read_string(spec, "borderStyle", &style);
        ui_read_string(spec, "borderColor", &color);
        ui_read_integer(spec, "borderWidth", &border_width);
        style_border(control, style, border_width, color);
    }
    if (json_object_has_member(spec, "fontSize")) {
        int font_size;
        ui_read_integer(spec, "fontSize", &font_size);
        style_font_tree(control, font_size, TRUE);
    }
    resolve_control_colors(spec, &background, &foreground);
    g_object_set_data(G_OBJECT(control), "allwhither-control-root", GINT_TO_POINTER(1));
    if (background != NULL || foreground != NULL) {
        style_control_tree(control, background, foreground, TRUE);
    }
    action_runtime_register(runtime, control_id, control);
    GtkWidget *rendered = control;
    if (has_max_width) {
        rendered = max_width_container_new(control, max_width, centered);
        gtk_widget_set_hexpand(rendered, TRUE);
        gtk_widget_set_halign(rendered, GTK_ALIGN_FILL);
    }
    GtkWidget *size_parent = parent;
    GtkWidget *child = rendered;
    if (GTK_IS_ORIENTABLE(parent) &&
        gtk_orientable_get_orientation(GTK_ORIENTABLE(parent)) ==
            GTK_ORIENTATION_HORIZONTAL &&
        (centered || width_is_percent || height_is_percent || has_max_width)) {
        GtkWidget *slot = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_hexpand(slot, !has_max_width);
        gtk_widget_set_halign(rendered, GTK_ALIGN_FILL);
        if (has_max_width) {
            g_object_set_data(G_OBJECT(slot), "allwhither-fixed-width",
                              GINT_TO_POINTER(1));
        }
        gtk_box_append(GTK_BOX(slot), rendered);
        child = slot;
        size_parent = slot;
    }
    gtk_box_append(GTK_BOX(parent), child);
    if (json_object_has_member(spec, "media")) {
        track_responsive_visibility(
            rendered, json_object_get_array_member(spec, "media"));
    }
    if (width_is_percent || height_is_percent || has_max_width) {
        track_responsive_size(control, size_parent,
                              has_width, requested_width, width_is_percent, width_value,
                              has_height, requested_height, height_is_percent, height_value,
                              has_max_width, max_width,
                              has_max_width ? rendered : NULL);
    }
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
        gtk_label_set_wrap(GTK_LABEL(label), TRUE);
        gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_widget_set_hexpand(label, TRUE);
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
        JsonNode *actions_node = json_object_get_member(spec, "actions");
        if (actions_node != NULL && JSON_NODE_HOLDS_OBJECT(actions_node) &&
            json_object_has_member(json_node_get_object(actions_node), "enter")) {
            StatusBinding *enter_binding =
                new_status_binding(status, label, runtime, control_id, NULL);
            g_signal_connect_data(entry, "activate",
                                  G_CALLBACK(on_entry_activated),
                                  enter_binding, free_status_binding, 0);
        }
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
        gboolean is_percent;
        double height_value;
        JsonArray *children;
        ui_read_string(spec, "label", &label);
        ui_read_dimension(spec, "height", &is_percent, &height_value);
        height = is_percent ? -1 : (int) height_value;
        ui_read_array(spec, "children", &children);
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        GtkWidget *caption = gtk_label_new(label);
        gtk_widget_set_halign(caption, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(box), caption);
        GtkWidget *scroll = gtk_scrolled_window_new();
        gtk_widget_set_size_request(scroll, -1, height);
        if (is_percent) {
            gtk_widget_set_vexpand(scroll, TRUE);
            track_responsive_size(scroll, parent,
                                  FALSE, -1, FALSE, 0,
                                  TRUE, -1, TRUE, height_value,
                                  FALSE, 0, NULL);
        }
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
        JsonArray *children;
        ui_read_array(spec, "children", &children);
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_widget_set_hexpand(box, TRUE);
        gtk_widget_set_vexpand(box, TRUE);
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
