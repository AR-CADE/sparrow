#ifndef POPUP_H
#define POPUP_H
#include <sparrow/nonstd/wlroots-full.hpp>


class SparrowView;
class Output;
class Core;

class SparrowPopup
{
  public:
    uint32_t handle = 0;

    struct wlr_xdg_popup *xdg_popup     = nullptr;
    struct wlr_xdg_surface *xdg_surface = nullptr;

    // Parent can be a toplevel view or another popup
    SparrowView *parent_view   = nullptr;
    SparrowPopup *first_popup  = nullptr;
    SparrowPopup *parent_popup = nullptr;

    // Position relative to parent (from xdg_positioner)
    int pos_x = 0, pos_y = 0;
    int x = 0, y = 0;
    int width = 0, height = 0;

    // Multi-monitor support: inherit output from parent
    Output *current_output = nullptr;
    double output_scale    = 0;

    // Scene tree for wlroots rendering and input handling
    struct wlr_scene_tree *scene_tree = nullptr;

    int64_t texture_id = 0;
    bool texture_registered = false;
    struct wlr_buffer *locked_buffer = nullptr;
    bool unconstrained; // Whether unconstrain has been called

    // struct sparrow_cached_texture cache;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener commit;
    struct wl_listener reposition;
    struct wl_listener new_subsurface;
    struct wl_listener scene_tree_destroy;
};

void sparrow_focus_popup(SparrowPopup *popup);
void sparrow_new_xdg_popup(struct wl_listener *listener, void *data);
void sparrow_popup_damage_whole(SparrowPopup *popup);

#endif
