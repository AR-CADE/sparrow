#ifndef SPARROW_LAYER_SURFACE_HPP
#define SPARROW_LAYER_SURFACE_HPP

#include <stdint.h>
#include <sparrow/nonstd/wlroots-full.hpp>

class Output;
class SparrowView;

struct SparrowLayerSurface
{
    struct wlr_layer_surface_v1 *layer_surface = nullptr;
    uint32_t handle    = 0;
    int64_t texture_id = 0;
    bool texture_registered = false;

    int x     = 0;
    int y     = 0;
    int width = 0;
    int height = 0;
    struct wlr_box last_buffer_box = {};
    Output *current_output = nullptr;

    bool is_sensitive = false;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener commit;
    struct wl_listener new_popup;
    struct wl_listener new_subsurface;

    struct wl_list subsurfaces; // SparrowSubSurface::link
    struct wl_list link; // Core::layer_surfaces_list
};

void sparrow_layer_shell_init(struct wl_display *display);
SparrowLayerSurface *sparrow_find_layer_surface_by_handle(uint32_t handle);
void sparrow_layer_surfaces_update_on_output_change(Output *output);
void sparrow_layer_surface_update_geometry(SparrowLayerSurface *layer_surf);
void sparrow_layer_surface_recompute_sensitivity(SparrowLayerSurface *layer_surf);
void sparrow_recompute_all_layer_surfaces_sensitivity();
bool sparrow_get_layer_surface_buffer_box(SparrowLayerSurface *layer_surf,
    struct wlr_output *wlr_out, struct wlr_box *out_buf_box, bool check_mapped = true);
bool sparrow_get_sensitive_layer_surface_buffer_box(SparrowLayerSurface *layer_surf,
    struct wlr_output *wlr_out, struct wlr_box *out_buf_box, bool check_mapped = true);
SparrowLayerSurface *sparrow_find_layer_surface_by_surface(const struct wlr_surface *surface);
bool sparrow_has_exclusive_layer_surface();
bool sparrow_has_fullscreen_opaque_top_layer();
bool sparrow_has_fullscreen_translucent_top_layer();
bool sparrow_layer_surface_is_occluded(const SparrowLayerSurface *layer_surf, const Output *output);
bool sparrow_focus_fallback_bottom_layer_surface(
    const SparrowView *excluded_view = nullptr,
    const SparrowLayerSurface *excluded_layer_surf = nullptr);

#endif
