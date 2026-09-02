#include "wlroots_mock_plugin.h"
#include <cairo.h>
#include <cmath>
#include <cstdint>
#include <gdk/gdk.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Mock Pixel Buffer Texture with Cairo Vector Rendering
// ---------------------------------------------------------------------------

struct _MockTexture {
  FlPixelBufferTexture parent_instance;
  uint32_t width;
  uint32_t height;
  std::vector<uint8_t> buffer;
};

G_DECLARE_FINAL_TYPE(MockTexture, mock_texture, MOCK, TEXTURE,
                     FlPixelBufferTexture)
G_DEFINE_TYPE(MockTexture, mock_texture, fl_pixel_buffer_texture_get_type())

static gboolean mock_texture_copy_pixels(FlPixelBufferTexture *texture,
                                         const uint8_t **out_buffer,
                                         uint32_t *width, uint32_t *height,
                                         GError **error) {
  MockTexture *self = MOCK_TEXTURE(texture);
  *width = self->width;
  *height = self->height;
  *out_buffer = self->buffer.data();
  return TRUE;
}

static void mock_texture_class_init(MockTextureClass *klass) {
  FL_PIXEL_BUFFER_TEXTURE_CLASS(klass)->copy_pixels = mock_texture_copy_pixels;
}

static void mock_texture_init(MockTexture *self) {}

static void draw_rounded_rect(cairo_t *cr, double x, double y, double w,
                              double h, double r) {
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
  cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
  cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
  cairo_close_path(cr);
}

static MockTexture *mock_texture_new_for_app(uint32_t width, uint32_t height,
                                             const std::string &app_id,
                                             const std::string &title) {
  MockTexture *self =
      MOCK_TEXTURE(g_object_new(mock_texture_get_type(), nullptr));
  self->width = width;
  self->height = height;
  self->buffer.resize(width * height * 4);

  cairo_surface_t *surf = cairo_image_surface_create_for_data(
      self->buffer.data(), CAIRO_FORMAT_ARGB32, width, height, width * 4);
  cairo_t *cr = cairo_create(surf);

  // Background
  if (app_id == "Alacritty") {
    // Modern Dark Terminal
    cairo_set_source_rgb(cr, 0.09, 0.09, 0.12);
    cairo_paint(cr);

    // Titlebar
    cairo_set_source_rgb(cr, 0.14, 0.14, 0.18);
    cairo_rectangle(cr, 0, 0, width, 48);
    cairo_fill(cr);

    // Window control buttons
    cairo_set_source_rgb(cr, 0.95, 0.35, 0.35); // Red
    cairo_arc(cr, 24, 24, 7, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.95, 0.75, 0.25); // Yellow
    cairo_arc(cr, 46, 24, 7, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.35, 0.85, 0.45); // Green
    cairo_arc(cr, 68, 24, 7, 0, 2 * M_PI);
    cairo_fill(cr);

    // Title
    const char *user_env = getenv("USER");
    std::string user_name =
        (user_env && user_env[0] != '\0') ? user_env : "user";
    std::string title_text = user_name + "@sparrow-os: ~/sparrow (fish)";
    std::string prompt_text = user_name + "@sparrow-os ";

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 18);
    cairo_set_source_rgb(cr, 0.8, 0.8, 0.85);
    cairo_move_to(cr, 100, 31);
    cairo_show_text(cr, title_text.c_str());

    // Terminal Content
    cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 24);

    cairo_set_source_rgb(cr, 0.4, 0.9, 0.4);
    cairo_move_to(cr, 36, 100);
    cairo_show_text(cr, prompt_text.c_str());
    cairo_set_source_rgb(cr, 0.4, 0.7, 1.0);
    cairo_show_text(cr, "~/sparrow ");
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_show_text(cr, "$ ./build.sh server");

    cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
    cairo_move_to(cr, 36, 145);
    cairo_show_text(cr, "[1/31] Compiling C++ object src/surface/view.cpp.o");
    cairo_move_to(cr, 36, 185);
    cairo_show_text(cr, "[2/31] Linking target src/sparrow");
    cairo_set_source_rgb(cr, 0.3, 1.0, 0.5);
    cairo_move_to(cr, 36, 230);
    cairo_show_text(
        cr, "✓ Sparrow build complete! Running on WAYLAND_DISPLAY=wayland-2");

    // Prompt cursor
    cairo_set_source_rgb(cr, 0.4, 0.9, 0.4);
    cairo_move_to(cr, 36, 290);
    cairo_show_text(cr, prompt_text.c_str());
    cairo_set_source_rgb(cr, 0.4, 0.7, 1.0);
    cairo_show_text(cr, "~/sparrow ");
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_show_text(cr, "$ █");

  } else if (app_id == "info.cemu.Cemu") {
    // Cemu Game Emulator UI
    cairo_set_source_rgb(cr, 0.08, 0.12, 0.18);
    cairo_paint(cr);

    // Titlebar
    cairo_set_source_rgb(cr, 0.12, 0.18, 0.26);
    cairo_rectangle(cr, 0, 0, width, 52);
    cairo_fill(cr);

    // Menu Bar
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 18);
    cairo_set_source_rgb(cr, 0.8, 0.85, 0.95);
    cairo_move_to(cr, 24, 33);
    cairo_show_text(
        cr, "File      Emulation      Options      Tools      Debug      Help");

    // Game banner card
    draw_rounded_rect(cr, 48, 90, width - 96, height - 160, 16);
    cairo_set_source_rgb(cr, 0.05, 0.08, 0.14);
    cairo_fill(cr);

    // Game Title
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 38);
    cairo_set_source_rgb(cr, 0.2, 0.7, 1.0);
    cairo_move_to(cr, 80, 180);
    cairo_show_text(cr, "The Legend of Zelda: Breath of the Wild");

    // FPS / Performance badge
    draw_rounded_rect(cr, 80, 220, 200, 48, 8);
    cairo_set_source_rgb(cr, 0.15, 0.55, 0.35);
    cairo_fill(cr);
    cairo_set_font_size(cr, 22);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_move_to(cr, 100, 252);
    cairo_show_text(cr, "FPS: 60.00 (Vulkan)");

    // Subtitle info
    cairo_set_font_size(cr, 20);
    cairo_set_source_rgb(cr, 0.7, 0.75, 0.85);
    cairo_move_to(cr, 80, 320);
    cairo_show_text(cr,
                    "Shader Pipeline: Async Compiled (12,842 shaders ready)");
    cairo_move_to(cr, 80, 360);
    cairo_show_text(cr, "Resolution: 3840x2160 (Graphics Pack Active)");

  } else if (app_id == "firefox") {
    // Firefox Web Browser
    cairo_set_source_rgb(cr, 0.96, 0.96, 0.98);
    cairo_paint(cr);

    // Header bar
    cairo_set_source_rgb(cr, 0.18, 0.18, 0.24);
    cairo_rectangle(cr, 0, 0, width, 68);
    cairo_fill(cr);

    // URL Bar
    draw_rounded_rect(cr, 120, 14, width - 240, 40, 20);
    cairo_set_source_rgb(cr, 0.28, 0.28, 0.36);
    cairo_fill(cr);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 18);
    cairo_set_source_rgb(cr, 0.5, 0.9, 0.6);
    cairo_move_to(cr, 145, 40);
    cairo_show_text(cr, "🔒 https://");
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_show_text(cr, "github.com/AR-CADE/sparrow");

    // Web page content
    cairo_set_source_rgb(cr, 0.12, 0.12, 0.16);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 42);
    cairo_move_to(cr, 64, 150);
    cairo_show_text(cr, "Sparrow: The Next-Gen Flutter Wayland Compositor");

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 22);
    cairo_set_source_rgb(cr, 0.35, 0.35, 0.45);
    cairo_move_to(cr, 64, 200);
    cairo_show_text(cr, "High-performance Wayland compositor powered by "
                        "Flutter Impeller and wlroots.");

    // Content cards
    for (int i = 0; i < 3; ++i) {
      draw_rounded_rect(cr, 64 + i * 420, 240, 390, 280, 12);
      cairo_set_source_rgb(cr, 0.88, 0.90, 0.95);
      cairo_fill(cr);
    }

  } else {
    // Generic Modern App / Calculator
    cairo_set_source_rgb(cr, 0.15, 0.16, 0.20);
    cairo_paint(cr);

    // Title
    cairo_set_source_rgb(cr, 0.20, 0.22, 0.28);
    cairo_rectangle(cr, 0, 0, width, 54);
    cairo_fill(cr);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 22);
    cairo_set_source_rgb(cr, 0.9, 0.9, 0.95);
    cairo_move_to(cr, 32, 36);
    cairo_show_text(cr, title.c_str());

    // Calc Display
    draw_rounded_rect(cr, 64, 90, 500, 100, 12);
    cairo_set_source_rgb(cr, 0.10, 0.11, 0.14);
    cairo_fill(cr);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 48);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_move_to(cr, 460, 160);
    cairo_show_text(cr, "42");
  }

  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  return self;
}

// ---------------------------------------------------------------------------
// Mock Wlroots Plugin State
// ---------------------------------------------------------------------------

struct MockSurfaceInfo {
  int64_t handle;
  int64_t texture_id;
  std::string app_id;
  std::string title;
  int64_t width;
  int64_t height;
};

static FlMethodChannel *s_channel = nullptr;
static FlTextureRegistrar *s_texture_registrar = nullptr;
static GtkWindow *s_window = nullptr;
static GtkWidget *s_fl_view = nullptr;
static int64_t s_next_handle = 1;
static std::vector<MockSurfaceInfo> s_surfaces;
static size_t s_app_cycle_index = 0;

static void get_current_window_size(int *out_w, int *out_h) {
  int w = 0, h = 0;
  if (s_fl_view && gtk_widget_get_realized(s_fl_view)) {
    GtkAllocation alloc;
    gtk_widget_get_allocation(s_fl_view, &alloc);
    if (alloc.width > 100 && alloc.height > 100) {
      w = alloc.width;
      h = alloc.height;
    }
  }
  if (w <= 100 || h <= 100) {
    if (s_window && gtk_widget_get_realized(GTK_WIDGET(s_window))) {
      GtkAllocation alloc;
      gtk_widget_get_allocation(GTK_WIDGET(s_window), &alloc);
      if (alloc.width > 100 && alloc.height > 100) {
        w = alloc.width;
        h = alloc.height;
      }
    }
  }
  if (w <= 100 || h <= 100) {
    GdkDisplay *display = gdk_display_get_default();
    if (display) {
      GdkMonitor *monitor = gdk_display_get_primary_monitor(display);
      if (monitor) {
        GdkRectangle geom;
        gdk_monitor_get_geometry(monitor, &geom);
        w = geom.width;
        h = geom.height;
      }
    }
  }
  *out_w = (w > 0) ? w : 1920;
  *out_h = (h > 0) ? h : 1080;
}

static void send_output_added(int64_t id, const char *name, int64_t width,
                              int64_t height, int64_t refresh, double scale) {
  if (!s_channel)
    return;

  g_autoptr(FlValue) map = fl_value_new_map();
  fl_value_set_string_take(map, "id", fl_value_new_int(id));
  fl_value_set_string_take(map, "name", fl_value_new_string(name));
  fl_value_set_string_take(map, "make", fl_value_new_string("MockHardware"));
  fl_value_set_string_take(map, "model", fl_value_new_string("MockDisplay"));
  fl_value_set_string_take(map, "x", fl_value_new_int(0));
  fl_value_set_string_take(map, "y", fl_value_new_int(0));
  fl_value_set_string_take(map, "width", fl_value_new_int(width));
  fl_value_set_string_take(map, "height", fl_value_new_int(height));
  fl_value_set_string_take(map, "refresh", fl_value_new_int(refresh));
  fl_value_set_string_take(map, "scale", fl_value_new_float(scale));
  fl_value_set_string_take(map, "transform", fl_value_new_int(0));

  g_autoptr(FlValue) modes = fl_value_new_list();
  g_autoptr(FlValue) mode = fl_value_new_map();
  fl_value_set_string_take(mode, "width", fl_value_new_int(width));
  fl_value_set_string_take(mode, "height", fl_value_new_int(height));
  fl_value_set_string_take(mode, "refresh", fl_value_new_int(refresh));
  fl_value_append(modes, mode);
  fl_value_set_string_take(map, "modes", fl_value_ref(modes));

  fl_method_channel_invoke_method(s_channel, "output_added", map, nullptr,
                                  nullptr, nullptr);
  g_message("Mock: Sent output_added (%s, %ldx%ld)", name, width, height);
}

static void send_output_changed(int64_t id, int64_t width, int64_t height) {
  if (!s_channel)
    return;

  g_autoptr(FlValue) map = fl_value_new_map();
  fl_value_set_string_take(map, "id", fl_value_new_int(id));
  fl_value_set_string_take(map, "name", fl_value_new_string("WL-1"));
  fl_value_set_string_take(map, "make", fl_value_new_string("MockHardware"));
  fl_value_set_string_take(map, "model", fl_value_new_string("MockDisplay"));
  fl_value_set_string_take(map, "x", fl_value_new_int(0));
  fl_value_set_string_take(map, "y", fl_value_new_int(0));
  fl_value_set_string_take(map, "width", fl_value_new_int(width));
  fl_value_set_string_take(map, "height", fl_value_new_int(height));
  fl_value_set_string_take(map, "refresh", fl_value_new_int(60000));
  fl_value_set_string_take(map, "scale", fl_value_new_float(1.0));
  fl_value_set_string_take(map, "transform", fl_value_new_int(0));

  fl_method_channel_invoke_method(s_channel, "output_changed", map, nullptr,
                                  nullptr, nullptr);
  g_message("Mock: Sent output_changed (id=%ld, %ldx%ld)", id, width, height);
}

static void send_surface_geometry(int64_t handle, int64_t width,
                                  int64_t height) {
  if (!s_channel)
    return;

  g_autoptr(FlValue) map = fl_value_new_map();
  fl_value_set_string_take(map, "handle", fl_value_new_int(handle));
  fl_value_set_string_take(map, "width", fl_value_new_int(width));
  fl_value_set_string_take(map, "height", fl_value_new_int(height));
  fl_value_set_string_take(map, "buffer_width", fl_value_new_int(width));
  fl_value_set_string_take(map, "buffer_height", fl_value_new_int(height));
  fl_value_set_string_take(map, "geo_x", fl_value_new_int(0));
  fl_value_set_string_take(map, "geo_y", fl_value_new_int(0));

  fl_method_channel_invoke_method(s_channel, "surface_geometry", map, nullptr,
                                  nullptr, nullptr);
  g_message("Mock: Sent surface_geometry (handle=%ld, %ldx%ld)", handle, width,
            height);
}

static void send_surface_map(const MockSurfaceInfo &info) {
  if (!s_channel)
    return;

  g_autoptr(FlValue) map = fl_value_new_map();
  fl_value_set_string_take(map, "handle", fl_value_new_int(info.handle));
  fl_value_set_string_take(map, "texture_id",
                           fl_value_new_int(info.texture_id));
  fl_value_set_string_take(map, "x", fl_value_new_int(0));
  fl_value_set_string_take(map, "y", fl_value_new_int(0));
  fl_value_set_string_take(map, "width", fl_value_new_int(info.width));
  fl_value_set_string_take(map, "height", fl_value_new_int(info.height));
  fl_value_set_string_take(map, "buffer_width", fl_value_new_int(info.width));
  fl_value_set_string_take(map, "buffer_height", fl_value_new_int(info.height));
  fl_value_set_string_take(map, "geo_x", fl_value_new_int(0));
  fl_value_set_string_take(map, "geo_y", fl_value_new_int(0));
  fl_value_set_string_take(map, "client_pid", fl_value_new_int(1234));
  fl_value_set_string_take(map, "client_uid", fl_value_new_int(1000));
  fl_value_set_string_take(map, "client_gid", fl_value_new_int(1000));
  fl_value_set_string_take(map, "title",
                           fl_value_new_string(info.title.c_str()));
  fl_value_set_string_take(map, "app_id",
                           fl_value_new_string(info.app_id.c_str()));
  fl_value_set_string_take(map, "maximized", fl_value_new_int(1));
  fl_value_set_string_take(map, "activated", fl_value_new_int(1));
  fl_value_set_string_take(map, "uses_csd", fl_value_new_int(0));
  fl_value_set_string_take(map, "output_id", fl_value_new_int(1));
  fl_value_set_string_take(map, "output_scale", fl_value_new_float(1.0));
  fl_value_set_string_take(map, "min_width", fl_value_new_int(400));
  fl_value_set_string_take(map, "max_width", fl_value_new_int(3840));
  fl_value_set_string_take(map, "min_height", fl_value_new_int(300));
  fl_value_set_string_take(map, "max_height", fl_value_new_int(2160));

  fl_method_channel_invoke_method(s_channel, "surface_map", map, nullptr,
                                  nullptr, nullptr);
  g_message("Mock: Sent surface_map for '%s' (handle=%ld, size=%ldx%ld)",
            info.title.c_str(), info.handle, info.width, info.height);
}

static void spawn_next_mock_surface() {
  struct AppDef {
    std::string app_id;
    std::string title;
  };
  static const std::vector<AppDef> apps = {
      {"Alacritty", "Terminal (Alacritty)"},
      {"info.cemu.Cemu", "Cemu Emulator (Wii U)"},
      {"firefox", "Mozilla Firefox"},
      {"org.gnome.Calculator", "Calculator"},
  };

  const AppDef &def = apps[s_app_cycle_index % apps.size()];
  s_app_cycle_index++;

  int win_w = 1280, win_h = 720;
  get_current_window_size(&win_w, &win_h);

  int64_t handle = s_next_handle++;
  g_autoptr(MockTexture) texture =
      mock_texture_new_for_app(win_w, win_h, def.app_id, def.title);
  int64_t texture_id = handle;
  if (s_texture_registrar) {
    if (fl_texture_registrar_register_texture(s_texture_registrar,
                                              FL_TEXTURE(texture))) {
      texture_id = fl_texture_get_id(FL_TEXTURE(texture));
    }
  }

  MockSurfaceInfo info = {
      .handle = handle,
      .texture_id = texture_id,
      .app_id = def.app_id,
      .title = def.title,
      .width = win_w,
      .height = win_h,
  };
  s_surfaces.push_back(info);
  send_surface_map(info);
}

static void close_mock_surface(int64_t handle = -1) {
  if (s_surfaces.empty() || !s_channel)
    return;

  MockSurfaceInfo target;
  if (handle <= 0) {
    target = s_surfaces.back();
    s_surfaces.pop_back();
  } else {
    for (auto it = s_surfaces.begin(); it != s_surfaces.end(); ++it) {
      if (it->handle == handle) {
        target = *it;
        s_surfaces.erase(it);
        break;
      }
    }
  }

  g_autoptr(FlValue) map = fl_value_new_map();
  fl_value_set_string_take(map, "handle", fl_value_new_int(target.handle));
  fl_method_channel_invoke_method(s_channel, "surface_unmap", map, nullptr,
                                  nullptr, nullptr);
  g_message("Mock: Sent surface_unmap for handle=%ld", target.handle);
}

static void method_call_cb(FlMethodChannel *channel, FlMethodCall *method_call,
                           gpointer user_data) {
  const gchar *method = fl_method_call_get_name(method_call);
  FlValue *args = fl_method_call_get_args(method_call);

  if (g_strcmp0(method, "compositor_ready") == 0) {
    g_message("Mock: Received compositor_ready from Flutter");
    int win_w = 1280, win_h = 720;
    get_current_window_size(&win_w, &win_h);
    send_output_added(1, "WL-1", win_w, win_h, 60000, 1.0);

    g_autoptr(FlMethodResponse) response =
        FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
    fl_method_call_respond(method_call, response, nullptr);
    return;
  }

  if (g_strcmp0(method, "mock_spawn_surface") == 0) {
    g_message("Mock: mock_spawn_surface called");
    spawn_next_mock_surface();
    g_autoptr(FlMethodResponse) response =
        FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
    fl_method_call_respond(method_call, response, nullptr);
    return;
  }

  if (g_strcmp0(method, "surface_toplevel_set_size") == 0) {
    int64_t handle = -1, req_w = 0, req_h = 0;
    if (args && fl_value_get_type(args) == FL_VALUE_TYPE_LIST &&
        fl_value_get_length(args) >= 3) {
      FlValue *h_val = fl_value_get_list_value(args, 0);
      FlValue *w_val = fl_value_get_list_value(args, 1);
      FlValue *h2_val = fl_value_get_list_value(args, 2);
      if (h_val && fl_value_get_type(h_val) == FL_VALUE_TYPE_INT)
        handle = fl_value_get_int(h_val);
      if (w_val && fl_value_get_type(w_val) == FL_VALUE_TYPE_INT)
        req_w = fl_value_get_int(w_val);
      if (h2_val && fl_value_get_type(h2_val) == FL_VALUE_TYPE_INT)
        req_h = fl_value_get_int(h2_val);
    }
    if (handle > 0 && req_w > 0 && req_h > 0) {
      for (auto &surf : s_surfaces) {
        if (surf.handle == handle) {
          surf.width = req_w;
          surf.height = req_h;
          break;
        }
      }
      send_surface_geometry(handle, req_w, req_h);
    }
    g_autoptr(FlMethodResponse) response =
        FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
    fl_method_call_respond(method_call, response, nullptr);
    return;
  }

  if (g_strcmp0(method, "surface_toplevel_close") == 0) {
    int64_t handle = -1;
    if (args && fl_value_get_type(args) == FL_VALUE_TYPE_LIST &&
        fl_value_get_length(args) > 0) {
      FlValue *item = fl_value_get_list_value(args, 0);
      if (item && fl_value_get_type(item) == FL_VALUE_TYPE_INT) {
        handle = fl_value_get_int(item);
      }
    }
    g_message("Mock: surface_toplevel_close called for handle=%ld", handle);
    close_mock_surface(handle);
    g_autoptr(FlMethodResponse) response =
        FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
    fl_method_call_respond(method_call, response, nullptr);
    return;
  }

  if (g_strcmp0(method, "get_socket_paths") == 0) {
    g_autoptr(FlValue) res = fl_value_new_map();
    fl_value_set_string_take(res, "wayland",
                             fl_value_new_string("wayland-mock-0"));
    g_autoptr(FlMethodResponse) response =
        FL_METHOD_RESPONSE(fl_method_success_response_new(res));
    fl_method_call_respond(method_call, response, nullptr);
    return;
  }

  if (g_strcmp0(method, "is_compositor") == 0) {
    g_autoptr(FlValue) res = fl_value_new_int(1);
    g_autoptr(FlMethodResponse) response =
        FL_METHOD_RESPONSE(fl_method_success_response_new(res));
    fl_method_call_respond(method_call, response, nullptr);
    return;
  }

  // Handle all other methods (surface_focus, force_render_all_views,
  // set_direct_input_mode, etc.)
  g_autoptr(FlMethodResponse) response =
      FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
  fl_method_call_respond(method_call, response, nullptr);
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event,
                             gpointer user_data) {
  if (event->keyval == GDK_KEY_F1) {
    g_message("Mock: [F1] Spawning next mock application");
    spawn_next_mock_surface();
    return TRUE;
  }
  if (event->keyval == GDK_KEY_F2) {
    g_message("Mock: [F2] Closing latest mock application");
    close_mock_surface();
    return TRUE;
  }
  return FALSE;
}

static void on_window_size_allocate(GtkWidget *widget,
                                    GtkAllocation *allocation,
                                    gpointer user_data) {
  static int last_w = 0, last_h = 0;
  if (allocation->width > 100 && allocation->height > 100 &&
      (allocation->width != last_w || allocation->height != last_h)) {
    last_w = allocation->width;
    last_h = allocation->height;
    g_message("Mock: Window size changed to %dx%d, notifying Flutter", last_w,
              last_h);
    send_output_changed(1, last_w, last_h);
    for (auto &surf : s_surfaces) {
      surf.width = last_w;
      surf.height = last_h;
      send_surface_geometry(surf.handle, last_w, last_h);
    }
  }
}

void wlroots_mock_plugin_register(FlPluginRegistry *registry, GtkWindow *window,
                                  GtkWidget *view) {
  s_window = window;
  s_fl_view = view;
  g_autoptr(FlPluginRegistrar) registrar =
      fl_plugin_registry_get_registrar_for_plugin(registry,
                                                  "WlrootsMockPlugin");
  FlBinaryMessenger *messenger = fl_plugin_registrar_get_messenger(registrar);
  s_texture_registrar = fl_plugin_registrar_get_texture_registrar(registrar);

  g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();
  s_channel =
      fl_method_channel_new(messenger, "wlroots", FL_METHOD_CODEC(codec));

  fl_method_channel_set_method_call_handler(s_channel, method_call_cb, nullptr,
                                            nullptr);

  if (window) {
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press),
                     nullptr);
  }
  if (view) {
    g_signal_connect(view, "size-allocate", G_CALLBACK(on_window_size_allocate),
                     nullptr);
  } else if (window) {
    g_signal_connect(window, "size-allocate",
                     G_CALLBACK(on_window_size_allocate), nullptr);
  }

  g_message("Mock: wlroots MethodChannel plugin registered successfully! "
            "[F1=Spawn app, F2=Close app]");
}
