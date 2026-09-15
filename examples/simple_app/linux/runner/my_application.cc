#include "my_application.h"

#include <flutter_linux/flutter_linux.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#undef Bool
#undef None
#undef Status
#undef Success
#endif
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif
#include <wayland-client.h>

#include <chrono>
#include <cstring>
#include <string>

#include "messages.g.h"
#include "sparrow-ipc-v1-client-protocol.h"
#include "ipc_client.hpp"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "flutter/generated_plugin_registrant.h"

struct _MyApplication {
  GtkApplication parent_instance;
  char** dart_entrypoint_arguments;
};

G_DEFINE_TYPE(MyApplication, my_application, GTK_TYPE_APPLICATION)

static void first_frame_cb(MyApplication* self, FlView* view) {
  gtk_widget_show(gtk_widget_get_toplevel(GTK_WIDGET(view)));
}

static GtkWindow* s_window = nullptr;
static gboolean s_is_fullscreen = FALSE;
static gboolean s_is_maximized = FALSE;

static PigeonRunnerRunnerFlutterIpcApi* s_flutter_ipc_api = nullptr;
static struct wl_registry* s_wl_registry = nullptr;
static struct sparrow_ipc_manager_v1* s_sparrow_ipc_manager = nullptr;
static IpcClient s_ipc_client;
static guint s_ipc_watch_id = 0;

static gboolean on_window_state_event(GtkWidget* widget, GdkEventWindowState* event, gpointer user_data) {
  s_is_fullscreen = (event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) != 0;
  s_is_maximized = (event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) != 0;
  return FALSE;
}

static PigeonRunnerRunnerHostApiVTable runner_host_api_vtable = {
  .get_system_info = [](PigeonRunnerRunnerHostApiResponseHandle* response_handle, gpointer user_data) {
    int w = 1280, h = 720;
    if (s_window) {
      gtk_window_get_size(s_window, &w, &h);
    }
    const gchar* title = s_window ? gtk_window_get_title(s_window) : "sparrow_demo_app";
    const gchar* wayland_display = g_getenv("WAYLAND_DISPLAY");
    g_autoptr(PigeonRunnerRunnerSystemInfoData) data =
        pigeon_runner_runner_system_info_data_new(
            "GTK Desktop Runner",
            "1.0.0",
            wayland_display ? wayland_display : "X11",
            w, h,
            1.0,
            s_is_fullscreen,
            s_is_maximized,
            title ? title : "sparrow_demo_app",
            "sparrow.flutter.app");
    pigeon_runner_runner_host_api_respond_get_system_info(response_handle, data);
  },
  .set_window_title = [](const gchar* title, PigeonRunnerRunnerHostApiResponseHandle* response_handle, gpointer user_data) {
    if (s_window && title) {
      gtk_window_set_title(s_window, title);
    }
    pigeon_runner_runner_host_api_respond_set_window_title(response_handle, TRUE);
  },
  .set_fullscreen = [](gboolean enabled, PigeonRunnerRunnerHostApiResponseHandle* response_handle, gpointer user_data) {
    if (s_window) {
      if (enabled) {
        gtk_window_fullscreen(s_window);
      } else {
        gtk_window_unfullscreen(s_window);
      }
      s_is_fullscreen = enabled;
    }
    pigeon_runner_runner_host_api_respond_set_fullscreen(response_handle, TRUE);
  },
  .set_maximized = [](gboolean enabled, PigeonRunnerRunnerHostApiResponseHandle* response_handle, gpointer user_data) {
    if (s_window) {
      if (enabled) {
        gtk_window_maximize(s_window);
      } else {
        gtk_window_unmaximize(s_window);
      }
      s_is_maximized = enabled;
    }
    pigeon_runner_runner_host_api_respond_set_maximized(response_handle, TRUE);
  },
  .minimize = [](PigeonRunnerRunnerHostApiResponseHandle* response_handle, gpointer user_data) {
    if (s_window) {
      gtk_window_iconify(s_window);
    }
    pigeon_runner_runner_host_api_respond_minimize(response_handle, TRUE);
  },
  .ping = [](PigeonRunnerRunnerHostApiResponseHandle* response_handle, gpointer user_data) {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string timestamp_str = std::to_string(now);
    g_autoptr(PigeonRunnerRunnerPongData) pong =
        pigeon_runner_runner_pong_data_new(TRUE, timestamp_str.c_str());
    pigeon_runner_runner_host_api_respond_ping(response_handle, pong);
  }
};

static void on_compositor_notification(const std::string& method, const rapidjson::Value& params) {
  if (!s_flutter_ipc_api) {
    return;
  }
  std::string json_str;
  if (!params.IsNull()) {
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    params.Accept(writer);
    json_str = sb.GetString();
  }
  pigeon_runner_runner_flutter_ipc_api_on_notification(
      s_flutter_ipc_api,
      method.c_str(),
      json_str.empty() ? nullptr : json_str.c_str(),
      nullptr, nullptr, nullptr);
}

static void on_ipc_channel_received(int fd) {
  g_print("[GTK Runner] Received secure IPC socketpair (FD %d) from Sparrow compositor\n", fd);
  if (s_ipc_watch_id > 0) {
    g_source_remove(s_ipc_watch_id);
    s_ipc_watch_id = 0;
  }
  s_ipc_client.set_fd(fd);
  s_ipc_client.set_notification_handler(on_compositor_notification);

  GIOChannel* channel = g_io_channel_unix_new(fd);
  g_io_channel_set_close_on_unref(channel, FALSE);
  s_ipc_watch_id = g_io_add_watch(
      channel,
      static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR),
      [](GIOChannel* source, GIOCondition condition, gpointer data) -> gboolean {
        if (condition & (G_IO_HUP | G_IO_ERR)) {
          g_print("[GTK Runner] Sparrow IPC channel disconnected\n");
          s_ipc_client.close();
          s_ipc_watch_id = 0;
          if (s_flutter_ipc_api) {
            pigeon_runner_runner_flutter_ipc_api_on_notification(
                s_flutter_ipc_api, "ipcDisconnected", nullptr, nullptr, nullptr, nullptr);
          }
          return G_SOURCE_REMOVE;
        }
        s_ipc_client.dispatch_read();
        return G_SOURCE_CONTINUE;
      },
      nullptr);
  g_io_channel_unref(channel);

  if (s_flutter_ipc_api) {
    pigeon_runner_runner_flutter_ipc_api_on_notification(
        s_flutter_ipc_api, "ipcConnected", nullptr, nullptr, nullptr, nullptr);
  }
}

static const struct sparrow_ipc_manager_v1_listener s_ipc_manager_listener = {
  .ipc_channel = [](void* data, struct sparrow_ipc_manager_v1* manager, int32_t fd) {
    (void)data;
    (void)manager;
    on_ipc_channel_received(fd);
  },
};

static const struct wl_registry_listener s_registry_listener = {
  .global = [](void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    (void)data;
    (void)version;
    if (strcmp(interface, sparrow_ipc_manager_v1_interface.name) == 0) {
      g_print("[GTK Runner] Bound sparrow_ipc_manager_v1 global; requesting secure socketpair channel...\n");
      s_sparrow_ipc_manager = static_cast<struct sparrow_ipc_manager_v1*>(
          wl_registry_bind(registry, name, &sparrow_ipc_manager_v1_interface, 1));
      sparrow_ipc_manager_v1_add_listener(s_sparrow_ipc_manager, &s_ipc_manager_listener, nullptr);
      sparrow_ipc_manager_v1_get_ipc_channel(s_sparrow_ipc_manager, APPLICATION_ID);
    }
  },
  .global_remove = [](void* data, struct wl_registry* registry, uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
  },
};

static void setup_sparrow_ipc() {
#ifdef GDK_WINDOWING_WAYLAND
  GdkDisplay* gdk_disp = gdk_display_get_default();
  if (!gdk_disp || !GDK_IS_WAYLAND_DISPLAY(gdk_disp)) {
    g_print("[GTK Runner] Running on X11 or non-Wayland display ($WAYLAND_DISPLAY=%s); Compositor IPC disabled\n",
            g_getenv("WAYLAND_DISPLAY"));
    return;
  }
  struct wl_display* wl_disp = gdk_wayland_display_get_wl_display(gdk_disp);
  if (!wl_disp) {
    g_print("[GTK Runner] GDK Wayland display does not have wl_display; Compositor IPC disabled\n");
    return;
  }

  g_print("[GTK Runner] Querying Wayland registry for sparrow_ipc_manager_v1 on $WAYLAND_DISPLAY=%s...\n",
          g_getenv("WAYLAND_DISPLAY"));
  s_wl_registry = wl_display_get_registry(wl_disp);
  wl_registry_add_listener(s_wl_registry, &s_registry_listener, nullptr);
  wl_display_roundtrip(wl_disp);

  if (s_sparrow_ipc_manager) {
    wl_display_roundtrip(wl_disp);
  } else {
    g_print("[GTK Runner] sparrow_ipc_manager_v1 not advertised by current Wayland compositor ($WAYLAND_DISPLAY=%s). Compositor IPC disabled.\n",
            g_getenv("WAYLAND_DISPLAY"));
  }
#else
  g_print("[GTK Runner] GDK Wayland support not compiled in.\n");
#endif
}

static PigeonRunnerRunnerHostIpcApiVTable runner_host_ipc_api_vtable = {
  .get_compositor_info = [](PigeonRunnerRunnerHostIpcApiResponseHandle* response_handle, gpointer user_data) {
    if (!s_ipc_client.is_connected()) {
      pigeon_runner_runner_host_ipc_api_respond_get_compositor_info(response_handle, nullptr);
      return;
    }
    g_object_ref(response_handle);
    rapidjson::Document params;
    params.SetObject();
    s_ipc_client.send_request("getCompositorInfo", params, [response_handle](bool success, const rapidjson::Value& val) {
      if (!success || !val.IsObject()) {
        pigeon_runner_runner_host_ipc_api_respond_get_compositor_info(response_handle, nullptr);
        g_object_unref(response_handle);
        return;
      }
      const char* compositor = (val.HasMember("compositor") && val["compositor"].IsString()) ? val["compositor"].GetString() : "";
      const char* version = (val.HasMember("version") && val["version"].IsString()) ? val["version"].GetString() : "";
      const char* ipc_channel = (val.HasMember("ipc_channel") && val["ipc_channel"].IsString()) ? val["ipc_channel"].GetString() : "";
      int64_t surfaces_count = (val.HasMember("surfaces_count") && val["surfaces_count"].IsNumber()) ? val["surfaces_count"].GetInt64() : 0;
      double client_fps = (val.HasMember("client_fps") && val["client_fps"].IsNumber()) ? val["client_fps"].GetDouble() : 0.0;
      int64_t peer_pid = (val.HasMember("peer_pid") && val["peer_pid"].IsNumber()) ? val["peer_pid"].GetInt64() : 0;
      const char* app_id = (val.HasMember("app_id") && val["app_id"].IsString()) ? val["app_id"].GetString() : "";

      g_autoptr(PigeonRunnerCompositorSystemInfoData) info =
          pigeon_runner_compositor_system_info_data_new(
              compositor, version, ipc_channel, surfaces_count, client_fps, peer_pid, app_id);
      pigeon_runner_runner_host_ipc_api_respond_get_compositor_info(response_handle, info);
      g_object_unref(response_handle);
    });
  },
  .ping_compositor = [](PigeonRunnerRunnerHostIpcApiResponseHandle* response_handle, gpointer user_data) {
    if (!s_ipc_client.is_connected()) {
      pigeon_runner_runner_host_ipc_api_respond_ping_compositor(response_handle, nullptr);
      return;
    }
    g_object_ref(response_handle);
    rapidjson::Document params;
    params.SetObject();
    s_ipc_client.send_request("ping", params, [response_handle](bool success, const rapidjson::Value& val) {
      if (!success || !val.IsObject()) {
        pigeon_runner_runner_host_ipc_api_respond_ping_compositor(response_handle, nullptr);
        g_object_unref(response_handle);
        return;
      }
      gboolean pong = (val.HasMember("pong") && val["pong"].IsBool()) ? val["pong"].GetBool() : FALSE;
      int64_t timestamp_us = (val.HasMember("timestamp_us") && val["timestamp_us"].IsNumber()) ? val["timestamp_us"].GetInt64() : 0;
      int64_t peer_pid = (val.HasMember("peer_pid") && val["peer_pid"].IsNumber()) ? val["peer_pid"].GetInt64() : 0;

      g_autoptr(PigeonRunnerCompositorPongData) pong_data =
          pigeon_runner_compositor_pong_data_new(pong, timestamp_us, peer_pid);
      pigeon_runner_runner_host_ipc_api_respond_ping_compositor(response_handle, pong_data);
      g_object_unref(response_handle);
    });
  },
  .list_surfaces = [](PigeonRunnerRunnerHostIpcApiResponseHandle* response_handle, gpointer user_data) {
    if (!s_ipc_client.is_connected()) {
      pigeon_runner_runner_host_ipc_api_respond_list_surfaces(response_handle, nullptr);
      return;
    }
    g_object_ref(response_handle);
    rapidjson::Document params;
    params.SetObject();
    s_ipc_client.send_request("listSurfaces", params, [response_handle](bool success, const rapidjson::Value& val) {
      if (!success || !val.IsArray()) {
        pigeon_runner_runner_host_ipc_api_respond_list_surfaces(response_handle, nullptr);
        g_object_unref(response_handle);
        return;
      }

      g_autoptr(FlValue) surfaces_list = fl_value_new_list();
      for (const auto& item : val.GetArray()) {
        if (item.IsObject()) {
          const char* title = (item.HasMember("title") && item["title"].IsString()) ? item["title"].GetString() : "";
          int64_t handle = (item.HasMember("handle") && item["handle"].IsNumber()) ? item["handle"].GetInt64() : 0;
          const char* app_id = (item.HasMember("app_id") && item["app_id"].IsString()) ? item["app_id"].GetString() : "";
          int64_t width = (item.HasMember("width") && item["width"].IsNumber()) ? item["width"].GetInt64() : 0;
          int64_t height = (item.HasMember("height") && item["height"].IsNumber()) ? item["height"].GetInt64() : 0;
          gboolean fullscreen = (item.HasMember("fullscreen") && item["fullscreen"].IsBool()) ? item["fullscreen"].GetBool() : FALSE;
          gboolean maximized = (item.HasMember("maximized") && item["maximized"].IsBool()) ? item["maximized"].GetBool() : FALSE;
          gboolean activated = (item.HasMember("activated") && item["activated"].IsBool()) ? item["activated"].GetBool() : FALSE;

          g_autoptr(PigeonRunnerCompositorSurfaceData) surface =
              pigeon_runner_compositor_surface_data_new(
                  title, handle, app_id, width, height, fullscreen, maximized, activated);
          fl_value_append_take(
              surfaces_list,
              fl_value_new_custom_object(
                  pigeon_runner_compositor_surface_data_type_id,
                  G_OBJECT(surface)));
        }
      }
      pigeon_runner_runner_host_ipc_api_respond_list_surfaces(response_handle, surfaces_list);
      g_object_unref(response_handle);
    });
  },
  .close_surface = [](int64_t surface_handle, PigeonRunnerRunnerHostIpcApiResponseHandle* response_handle, gpointer user_data) {
    if (!s_ipc_client.is_connected()) {
      pigeon_runner_runner_host_ipc_api_respond_close_surface(response_handle, FALSE);
      return;
    }
    g_object_ref(response_handle);
    rapidjson::Document params;
    params.SetObject();
    params.AddMember("handle", surface_handle, params.GetAllocator());
    s_ipc_client.send_request("closeSurface", params, [response_handle](bool success, const rapidjson::Value& val) {
      gboolean closed = FALSE;
      if (success && val.IsObject() && val.HasMember("closed") && val["closed"].IsBool()) {
        closed = val["closed"].GetBool();
      }
      pigeon_runner_runner_host_ipc_api_respond_close_surface(response_handle, closed);
      g_object_unref(response_handle);
    });
  },
};

// Implements GApplication::activate.
static void my_application_activate(GApplication* application) {
  MyApplication* self = MY_APPLICATION(application);
  GtkWindow* window =
      GTK_WINDOW(gtk_application_window_new(GTK_APPLICATION(application)));

  // Use a header bar when running in GNOME as this is the common style used
  // by applications and is the setup most users will be using (e.g. Ubuntu
  // desktop).
  // If running on X and not using GNOME then just use a traditional title bar
  // in case the window manager does more exotic layout, e.g. tiling.
  // If running on Wayland assume the header bar will work (may need changing
  // if future cases occur).
  gboolean use_header_bar = TRUE;
#ifdef GDK_WINDOWING_X11
  GdkScreen* screen = gtk_window_get_screen(window);
  if (GDK_IS_X11_SCREEN(screen)) {
    const gchar* wm_name = gdk_x11_screen_get_window_manager_name(screen);
    if (g_strcmp0(wm_name, "GNOME Shell") != 0) {
      use_header_bar = FALSE;
    }
  }
#endif
  if (use_header_bar) {
    GtkHeaderBar* header_bar = GTK_HEADER_BAR(gtk_header_bar_new());
    gtk_widget_show(GTK_WIDGET(header_bar));
    gtk_header_bar_set_title(header_bar, "sparrow_demo_app");
    gtk_header_bar_set_show_close_button(header_bar, TRUE);
    gtk_window_set_titlebar(window, GTK_WIDGET(header_bar));
  } else {
    gtk_window_set_title(window, "sparrow_demo_app");
  }

  gtk_window_set_default_size(window, 1280, 720);

  g_autoptr(FlDartProject) project = fl_dart_project_new();
  fl_dart_project_set_dart_entrypoint_arguments(
      project, self->dart_entrypoint_arguments);

  FlView* view = fl_view_new(project);
  GdkRGBA background_color;
  // Background defaults to black, override it here if necessary, e.g. #00000000
  // for transparent.
  gdk_rgba_parse(&background_color, "#000000");
  fl_view_set_background_color(view, &background_color);
  gtk_widget_show(GTK_WIDGET(view));
  gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(view));

  // Show the window when Flutter renders.
  // Requires the view to be realized so we can start rendering.
  g_signal_connect_swapped(view, "first-frame", G_CALLBACK(first_frame_cb),
                           self);
  gtk_widget_realize(GTK_WIDGET(view));

  s_window = window;
  g_signal_connect(window, "window-state-event", G_CALLBACK(on_window_state_event), nullptr);

  FlBinaryMessenger* messenger =
      fl_engine_get_binary_messenger(fl_view_get_engine(view));
  s_flutter_ipc_api = pigeon_runner_runner_flutter_ipc_api_new(messenger, nullptr);

  pigeon_runner_runner_host_api_set_method_handlers(
      messenger, nullptr, &runner_host_api_vtable, self, nullptr);
  pigeon_runner_runner_host_ipc_api_set_method_handlers(
      messenger, nullptr, &runner_host_ipc_api_vtable, self, nullptr);

  fl_register_plugins(FL_PLUGIN_REGISTRY(view));

  gtk_widget_grab_focus(GTK_WIDGET(view));

  setup_sparrow_ipc();
}

// Implements GApplication::local_command_line.
static gboolean my_application_local_command_line(GApplication* application,
                                                  gchar*** arguments,
                                                  int* exit_status) {
  MyApplication* self = MY_APPLICATION(application);
  // Strip out the first argument as it is the binary name.
  self->dart_entrypoint_arguments = g_strdupv(*arguments + 1);

  g_autoptr(GError) error = nullptr;
  if (!g_application_register(application, nullptr, &error)) {
    g_warning("Failed to register: %s", error->message);
    *exit_status = 1;
    return TRUE;
  }

  g_application_activate(application);
  *exit_status = 0;

  return TRUE;
}

// Implements GApplication::startup.
static void my_application_startup(GApplication* application) {
  G_APPLICATION_CLASS(my_application_parent_class)->startup(application);
}

// Implements GApplication::shutdown.
static void my_application_shutdown(GApplication* application) {
  if (s_ipc_watch_id > 0) {
    g_source_remove(s_ipc_watch_id);
    s_ipc_watch_id = 0;
  }
  s_ipc_client.close();
  if (s_sparrow_ipc_manager) {
    sparrow_ipc_manager_v1_destroy(s_sparrow_ipc_manager);
    s_sparrow_ipc_manager = nullptr;
  }
  if (s_wl_registry) {
    wl_registry_destroy(s_wl_registry);
    s_wl_registry = nullptr;
  }
  if (s_flutter_ipc_api) {
    g_object_unref(s_flutter_ipc_api);
    s_flutter_ipc_api = nullptr;
  }

  G_APPLICATION_CLASS(my_application_parent_class)->shutdown(application);
}

// Implements GObject::dispose.
static void my_application_dispose(GObject* object) {
  MyApplication* self = MY_APPLICATION(object);
  g_clear_pointer(&self->dart_entrypoint_arguments, g_strfreev);
  G_OBJECT_CLASS(my_application_parent_class)->dispose(object);
}

static void my_application_class_init(MyApplicationClass* klass) {
  G_APPLICATION_CLASS(klass)->activate = my_application_activate;
  G_APPLICATION_CLASS(klass)->local_command_line =
      my_application_local_command_line;
  G_APPLICATION_CLASS(klass)->startup = my_application_startup;
  G_APPLICATION_CLASS(klass)->shutdown = my_application_shutdown;
  G_OBJECT_CLASS(klass)->dispose = my_application_dispose;
}

static void my_application_init(MyApplication* self) {}

MyApplication* my_application_new() {
  g_set_prgname(APPLICATION_ID);

  return MY_APPLICATION(g_object_new(my_application_get_type(),
                                     "application-id", APPLICATION_ID, "flags",
                                     G_APPLICATION_NON_UNIQUE, nullptr));
}
