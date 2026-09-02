#ifndef VIEW_H
#define VIEW_H
#include <sparrow/nonstd/wlroots-full.hpp>

class Output;
class Core;

class SparrowView
{
  public:
    struct wl_list link;
    uint32_t handle = 0;

    struct wlr_xdg_toplevel *toplevel   = nullptr;
    struct wlr_xdg_surface *xdg_surface = nullptr;

    int x     = 0;
    int y     = 0;
    int width = 0;
    int height = 0;

    // Multi-monitor support: track which output this view is on
    Output *current_output = nullptr; // Cached output for this surface
    double output_scale    = 0;          // Cached scale (for rendering)

    // Geometry offset - where visible content starts within the buffer
    // Used by CSD apps that include shadows in their buffer
    int geo_x = 0;
    int geo_y = 0;

    bool maximized  = false;
    bool fullscreen = false;
    bool activated  = false;

    // Decoration tracking - true if we successfully negotiated SSD with the
    // client
    struct wlr_xdg_toplevel_decoration_v1 *decoration = nullptr;
    bool uses_ssd = false;
    struct wl_listener decoration_request_mode;
    struct wl_listener decoration_destroy;

    // Flutter external texture ID for this surface (same as handle for
    // simplicity)
    int64_t texture_id = 0;
    bool texture_registered = false;
    struct wlr_buffer *locked_buffer = nullptr;

    // Frame pacing & drop diagnostics
    uint64_t commit_count  = 0;
    uint64_t sampled_count = 0;
    uint64_t dropped_buffer_count   = 0;
    uint64_t duplicate_sample_count = 0;
    bool current_buffer_sampled     = true;
    uint64_t last_commit_time_us    = 0;
    uint64_t last_sample_time_us    = 0;
    struct wlr_buffer *last_sampled_buffer = nullptr;

    // struct sparrow_cached_texture cache;

    // Subsurface tracking
    struct wl_list subsurfaces; // List of SparrowSubSurface
    struct wl_listener new_subsurface;

    struct wlr_scene_tree *scene_tree     = nullptr;
    struct wlr_scene_tree *scene_xdg_tree = nullptr;
    struct wlr_scene_rect *scene_frame    = nullptr;
    struct wlr_scene_rect *scene_titlebar = nullptr;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener commit;
    struct wl_listener set_title;
    struct wl_listener set_app_id;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_minimize;
    struct wl_listener request_maximize;

    // Foreign toplevel management protocols
    struct wlr_foreign_toplevel_handle_v1 *foreign_toplevel = nullptr;
    struct wl_listener foreign_activate_request;
    struct wl_listener foreign_close_request;
    struct wl_listener foreign_maximize_request;
    struct wl_listener foreign_minimize_request;
    struct wl_listener foreign_fullscreen_request;

    struct wlr_ext_foreign_toplevel_handle_v1 *ext_foreign_toplevel = nullptr;

    // Synchronized resize state - delays widget resize until client commits
    // matching buffer
    bool resize_in_progress = false; // True during interactive resize
    uint32_t pending_configure_serial =
        0; // Serial we're waiting for ack (unused for now, reserved)
    int pending_width  = 0;          // Requested content width
    int pending_height = 0; // Requested content height
    uint64_t resize_request_id = 0; // Dart's request ID for correlation
};

void sparrow_view_damage_whole(SparrowView *view);
void sparrow_view_damage_add_rect(SparrowView *view, int x, int y, int width,
    int height);
bool sparrow_view_get_scene_box(const SparrowView *view, struct wlr_box *out_box);
bool sparrow_view_filter_occluded_damage(const SparrowView *view,
    pixman_region32_t *damage,
    pixman_region32_t *visible_damage_out);
void sparrow_view_focus(SparrowView *view);
void sparrow_view_update_scene(SparrowView *view);
void sparrow_view_create_scene(SparrowView *view);
void sparrow_view_destroy_scene(SparrowView *view);

#endif
