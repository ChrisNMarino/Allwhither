#ifndef OMNI_MENU_RENDERER_H
#define OMNI_MENU_RENDERER_H

#include <gtk/gtk.h>
#include <json-glib/json-glib.h>

GtkWidget *menu_renderer_create(GtkApplication *application,
                                JsonArray *menus,
                                GtkWidget *status);

#endif
