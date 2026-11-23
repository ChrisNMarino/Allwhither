#ifndef OMNI_ACTION_RUNTIME_H
#define OMNI_ACTION_RUNTIME_H

#include <gtk/gtk.h>
#include <json-glib/json-glib.h>

typedef struct _ActionRuntime ActionRuntime;

ActionRuntime *action_runtime_new(JsonObject *controls,
                                 JsonArray *scripts,
                                 GtkWidget *status,
                                 GError **error);
void action_runtime_register(ActionRuntime *runtime,
                             const char *control_id,
                             GtkWidget *widget);
void action_runtime_invoke(ActionRuntime *runtime,
                           const char *control_id,
                           const char *event_type);
void action_runtime_set_window(ActionRuntime *runtime,
                               GtkWindow *window);
void action_runtime_set_container(ActionRuntime *runtime,
                                  GtkWidget *container,
                                  gboolean centered);
void action_runtime_free(gpointer runtime);

#endif
