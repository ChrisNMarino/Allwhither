#ifndef OMNI_CONTROL_RENDERER_H
#define OMNI_CONTROL_RENDERER_H

#include <gtk/gtk.h>
#include <json-glib/json-glib.h>

#include "action_runtime.h"

void control_renderer_set_theme(const char *background, const char *foreground);
void control_renderer_style_widget(GtkWidget *widget,
                                   const char *background,
                                   const char *foreground);
void control_renderer_style_tree(GtkWidget *widget,
                                 const char *background,
                                 const char *foreground);
const char *control_renderer_contrast_foreground(const char *background);
gboolean control_renderer_render(const char *control_id,
                                 JsonObject *spec,
                                 JsonObject *controls,
                                 GtkWidget *parent,
                                 GtkWidget *status,
                                 ActionRuntime *runtime);

#endif
