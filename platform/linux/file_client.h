#ifndef OMNI_FILE_CLIENT_H
#define OMNI_FILE_CLIENT_H

#include <gtk/gtk.h>
#include <JavaScriptCore/JavaScript.h>

typedef struct _FileClient FileClient;

FileClient *file_client_new(JSGlobalContextRef context, const char *project_name);
void file_client_set_window(FileClient *client, GtkWindow *window);
JSObjectRef file_client_js_object(FileClient *client);
void file_client_free(FileClient *client);

#endif
