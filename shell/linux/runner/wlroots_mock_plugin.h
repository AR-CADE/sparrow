#ifndef WLROOTS_MOCK_PLUGIN_H_
#define WLROOTS_MOCK_PLUGIN_H_

#include <flutter_linux/flutter_linux.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

void wlroots_mock_plugin_register(FlPluginRegistry *registry, GtkWindow *window, GtkWidget *view);

G_END_DECLS

#endif  // WLROOTS_MOCK_PLUGIN_H_
