#ifndef SUB_SURFACE_H
#define SUB_SURFACE_H
#include <sparrow/nonstd/wlroots-full.hpp>

class SparrowView;
struct SparrowLayerSurface;
class Output;
class Core;

constexpr uint32_t kLayerSubsurfaceParentOffset = 300000u;

class SparrowSubSurface
{
  public:
    struct wl_list link; // Link in parent view/layer surface subsurface list
    uint32_t handle = 0; // Unique handle for this subsurface
    uint32_t parent_handle = 0; // Cached parent handle

    SparrowView *parent_view = nullptr;
    SparrowLayerSurface *parent_layer_surface = nullptr;
    struct wlr_subsurface *wlr_subsurface     = nullptr;
    struct wlr_surface *surface = nullptr;

    int x = 0, y = 0; // Position relative to parent
    int width = 0, height = 0;
    int buffer_width = 0, buffer_height = 0;

    // Multi-monitor support: inherit output from parent
    Output *current_output = nullptr;
    double output_scale    = 0;

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
    uint64_t last_summary_commits = 0;
    uint64_t last_summary_samples = 0;
    uint64_t last_summary_ghosts  = 0;

    // struct sparrow_cached_texture cache;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener commit;
    struct wl_listener new_subsurface;
};

// Forward declarations for subsurface handling
void sparrow_subsurface_handle_new(struct wl_listener *listener, void *data);
void sparrow_subsurface_handle_new_layer(struct wl_listener *listener, void *data);
SparrowSubSurface *sparrow_subsurface_create(SparrowView *view,
    struct wlr_subsurface *wlr_subsurface);
SparrowSubSurface *sparrow_subsurface_create_layer(SparrowLayerSurface *layer_surf,
    struct wlr_subsurface *wlr_subsurface);
void sparrow_subsurface_damage_add_rect(SparrowSubSurface *sub, int x, int y,
    int width, int height, bool schedule_output_frame = true);
void sparrow_sub_surface_get_root_coords(SparrowSubSurface *sub,
    int *root_x, int *root_y);

#endif
