#ifndef WAYLAND_WINDOW_HPP
#define WAYLAND_WINDOW_HPP

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <wayland-cursor.h>
#include "xdg-shell-client-protocol.h"
#include "cursor-shape-v1-client-protocol.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

class WaylandWindow
{
  public:
    WaylandWindow();
    ~WaylandWindow();

    bool init(const std::string & app_id,
        const std::string & title,
        int32_t initial_width  = 1280,
        int32_t initial_height = 720,
        bool fullscreen = false,
        bool maximized  = false,
        bool enable_egl = true);

    void shutdown();
    void dispatch_events();
    bool is_running() const
    {
        return running_;
    }

    void request_close()
    {
        running_ = false;
    }

    // EGL rendering callbacks
    bool make_current();
    bool clear_current();
    bool make_resource_current();
    bool swap_buffers();
    static void * gl_proc_resolver(void *user_data, const char *name);

    // Accessors
    struct wl_display * get_display() const
    {
        return display_;
    }

    struct wl_compositor * get_compositor() const
    {
        return compositor_;
    }

    struct wp_cursor_shape_manager_v1 * get_cursor_shape_manager() const
    {
        return cursor_shape_manager_;
    }

    struct wl_surface * get_surface() const
    {
        return surface_;
    }

    struct wl_seat * get_seat() const
    {
        return seat_;
    }

    struct wl_shm * get_shm() const
    {
        return shm_;
    }

    int32_t get_width() const
    {
        return width_;
    }

    int32_t get_height() const
    {
        return height_;
    }

    double get_pixel_ratio() const
    {
        return pixel_ratio_;
    }

    bool is_fullscreen() const
    {
        return fullscreen_;
    }

    bool is_maximized() const
    {
        return maximized_;
    }

    const std::string& get_title() const
    {
        return title_;
    }

    const std::string& get_app_id() const
    {
        return app_id_;
    }

    void set_title(const std::string & title);
    void set_app_id(const std::string & app_id);
    void set_fullscreen(bool fullscreen);
    void set_maximized(bool maximized);
    void minimize();

    // Clipboard API
    void set_clipboard_text(const std::string & text);
    std::string get_clipboard_text();
    bool has_clipboard_text();
    void set_last_serial(uint32_t serial)
    {
        last_serial_ = serial;
    }

    uint32_t get_last_serial() const
    {
        return last_serial_;
    }

    const std::string& get_cached_clipboard_text() const
    {
        return clipboard_text_;
    }

    // Callbacks
    std::function<void(int32_t width, int32_t height, double pixel_ratio)> on_window_metrics_changed;
    std::function<void(int32_t width, int32_t height)> on_window_resized;
    std::function<void()> on_close_requested;
    std::function<void(struct wl_seat *seat)> on_seat_bound;
    std::function<void(int fd)> on_ipc_fd_received;

    int get_ipc_fd() const
    {
        return ipc_fd_;
    }

    void set_ipc_client(class IpcClient *client);
    void set_repeat_timer(int fd, std::function<void()> on_timer);

    // Internal Wayland protocol handlers
    void handle_ipc_channel(int fd);
    void handle_registry_global(struct wl_registry *registry, uint32_t name, const char *interface,
        uint32_t version);
    void handle_registry_global_remove(struct wl_registry *registry, uint32_t name);
    void handle_xdg_surface_configure(struct xdg_surface *surface, uint32_t serial);
    void handle_xdg_toplevel_configure(struct xdg_toplevel *toplevel, int32_t width, int32_t height,
        struct wl_array *states);
    void handle_xdg_toplevel_close(struct xdg_toplevel *toplevel);
    void handle_surface_enter(struct wl_surface *surface, struct wl_output *output);
    void handle_surface_leave(struct wl_surface *surface, struct wl_output *output);

    // Wayland Data Device protocol handlers
    void bind_data_device();
    void handle_data_offer(struct wl_data_offer *offer);
    void handle_selection(struct wl_data_offer *offer);
    void handle_offer_mime_type(struct wl_data_offer *offer, const char *mime_type);
    void handle_data_source_send(struct wl_data_source *source, const char *mime_type, int32_t fd);
    void handle_data_source_cancelled(struct wl_data_source *source);

  private:
    bool init_egl();
    void destroy_egl();

    bool enable_egl_ = true;

    struct wl_display *display_   = nullptr;
    struct wl_registry *registry_ = nullptr;
    struct wl_compositor *compositor_ = nullptr;
    struct wl_subcompositor *subcompositor_ = nullptr;
    struct wl_shm *shm_   = nullptr;
    struct wl_seat *seat_ = nullptr;
    struct xdg_wm_base *xdg_wm_base_ = nullptr;
    struct wp_cursor_shape_manager_v1 *cursor_shape_manager_ = nullptr;
    struct sparrow_ipc_manager_v1 *ipc_manager_ = nullptr;
    class IpcClient *ipc_client_ = nullptr;
    int ipc_fd_ = -1;

    // Wayland Data Device members
    struct wl_data_device_manager *data_device_manager_ = nullptr;
    struct wl_data_device *data_device_ = nullptr;
    struct wl_data_source *current_data_source_ = nullptr;
    struct wl_data_offer *current_data_offer_   = nullptr;
    std::vector<std::string> current_offer_mime_types_;
    struct wl_data_offer *pending_offer_ = nullptr;
    std::vector<std::string> pending_mimes_;
    uint32_t last_serial_ = 0;
    std::string clipboard_text_;

    struct wl_surface *surface_ = nullptr;
    struct xdg_surface *xdg_surface_   = nullptr;
    struct xdg_toplevel *xdg_toplevel_ = nullptr;
    struct wl_egl_window *egl_window_  = nullptr;

    EGLDisplay egl_display_ = EGL_NO_DISPLAY;
    EGLConfig egl_config_   = nullptr;
    EGLContext egl_context_ = EGL_NO_CONTEXT;
    EGLContext egl_resource_context_ = EGL_NO_CONTEXT;
    EGLSurface egl_surface_ = EGL_NO_SURFACE;

    int32_t width_  = 1280;
    int32_t height_ = 720;
    double pixel_ratio_ = 1.0;
    std::string app_id_;
    std::string title_;
    bool configured_ = false;
    bool running_    = true;
    bool fullscreen_ = false;
    bool maximized_  = false;
    int repeat_timer_fd_ = -1;
    std::function<void()> on_repeat_timer_;
};

#endif // WAYLAND_WINDOW_HPP
