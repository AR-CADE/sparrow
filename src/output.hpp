#ifndef OUTPUT_H
#define OUTPUT_H

#include <mutex>
#include <sparrow/nonstd/wlroots-full.hpp>

// Forward declarations
class Core;
class SparrowView;
class Output
{
  public:
    struct wl_list link;

    // Unique output ID for platform channel communication,
    // -1 means unitialized
    uint32_t id = -1;
    struct wlr_output *wlr_output = nullptr;
    struct wlr_scene_output *scene_output = nullptr;
    struct wlr_output_layout_output *layout_output = nullptr;

    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener present;
    struct wl_listener needs_frame;
    struct wl_listener damage;
    struct wl_listener destroy; // Handle monitor disconnect/hotplug

    struct wlr_damage_ring damage_ring;
    pixman_region32_t client_damage;
    std::mutex damage_mutex;

#ifdef DAMAGE_HISTORY
    #define NUM_DAMAGE_HISTORY 3
    pixman_region32_t damage_history[NUM_DAMAGE_HISTORY];
    int damage_history_idx = 0;
#endif

    // FPS & Frame pacing tracking
    uint64_t last_frame_time_us     = 0;
    uint64_t frame_intervals_us[30] = {0};
    int frame_interval_idx   = 0;
    int frame_interval_count = 0;
    double smoothed_fps = 0.0;
    double smoothed_render_ms = 0.0;

    // Triple Buffering active state
    bool triple_buffering_active   = true;
    bool vsync_dispatched_in_frame = false;

    // Last committed hardware cursor state on this output (to avoid spurious
    // empty KMS pageflips when wlr_output->needs_frame was set by
    // wlr_output_schedule_frame rather than an actual cursor plane change)
    const struct wlr_output_cursor *last_hw_cursor = nullptr;
    const struct wlr_buffer *last_hw_cursor_buffer = nullptr;
    double last_hw_cursor_x = 0.0;
    double last_hw_cursor_y = 0.0;
    int32_t last_hw_cursor_hotspot_x = 0;
    int32_t last_hw_cursor_hotspot_y = 0;
    bool last_hw_cursor_visible = false;

    // Pacing & VBlank diagnostics
    uint64_t last_flip_time_us = 0;
    uint64_t flip_count = 0;
    uint64_t missed_vblank_count     = 0;
    uint64_t last_pacing_log_time_us = 0;
    uint64_t last_state_change_us    = 0;

    // Direct Scanout / Direct Draw telemetry
    uint64_t direct_scanout_flips = 0;
    uint64_t direct_draw_flips    = 0;
    uint64_t composited_flips     = 0;
    bool direct_mode_active = false;

    // Cached result of wlr_output_test_state for Direct Scanout to avoid per-frame TEST_ONLY ioctls
    struct ScanoutTestCache
    {
        uint32_t format   = 0;
        uint64_t modifier = 0;
        int width   = 0;
        int height  = 0;
        bool valid  = false;
        bool result = false;
    } scanout_test_cache;

    // Estimated VBlank pacing timer when !needs_render (nanosecond timerfd)
    int vblank_timer_fd = -1;
    struct wl_event_source *vblank_timer_source = nullptr;
    uint64_t last_present_time_ns = 0;
    uint64_t refresh_duration_ns  = 16666666;
    uint64_t current_cycle_seq    = 0;
    uint64_t last_frame_done_cycle_seq = 0;

    // Cached FPS OSD texture
    struct wlr_texture *osd_texture = nullptr;
    char last_osd_text[80] = {0};
    uint64_t last_osd_update_us = 0;
};

#ifdef DAMAGE_HISTORY
void sparrow_output_damage_history_reset(Output *output, int width, int height);
#endif

void sparrow_server_new_output(struct wl_listener *listener, void *data);
void sparrow_engine_vsync_callback(void *data, intptr_t baton);
void sparrow_damage_add_box(const struct wlr_box *box,
    bool is_client_damage = false, bool schedule_output_frame = true);
bool sparrow_view_is_in_direct_mode(const SparrowView *view);
bool sparrow_surface_is_fully_opaque(const struct wlr_surface *surface);
bool sparrow_output_has_fullscreen_opaque_view(const Output *output);
bool sparrow_output_has_fullscreen_opaque_overlay(const Output *output);

// Multi-monitor support: determine which output a box is on (by center point)
Output *sparrow_output_for_box(int x, int y, int width, int height);

// Vsync driver selection
void sparrow_select_highest_refresh_output();
void sparrow_set_vsync_output(uint32_t output_id);
void sparrow_set_vsync_rate_limit(int max_hz);

// Output configuration
bool sparrow_set_output_mode(uint32_t output_id, int width,
    int height, int refresh);
bool sparrow_set_output_position(uint32_t output_id, int x, int y);
bool sparrow_set_output_scale(uint32_t output_id, double scale);
bool sparrow_set_output_transform(uint32_t output_id, int transform);

Output *sparrow_get_first_output();

// Output management protocol (wlr_output_manager_v1)
void sparrow_output_manager_init();
void sparrow_output_manager_update();

// Output power management protocol (wlr_output_power_management_v1)
void sparrow_output_power_manager_init();
bool sparrow_has_enabled_output();
void sparrow_output_update_dpms_lifecycle();

int get_output_refresh(struct wlr_output *output);

#endif
