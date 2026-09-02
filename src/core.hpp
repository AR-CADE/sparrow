#ifndef CORE_H
#define CORE_H

#include "sparrow/options.hpp"

#ifndef SPARROW_VERSION
    #define SPARROW_VERSION ""
#endif

#include "flutter_embedder.h"

#include "client_wrapper/binary_messenger.hpp"
#include "client_wrapper/incoming_message_dispatcher.hpp"
#include "client_wrapper/method_channel.h"
#include "client_wrapper/encodable_value.h"
#include "flutter/platform/task.hpp"
#include "input/seat.hpp"
#include "output.hpp"
#include "renderer/renderer.hpp"
#include "util/handle_map.hpp"
#include "util/rethreading/callable_queue.hpp"
#include "util/rethreading/debouncer.hpp"
#include <atomic>
#include <sparrow/nonstd/wlroots-full.hpp>

class SparrowView;
class SparrowSubSurface;
class SparrowPopup;

class Core
{
  private:
    Core()
    {}

    static std::atomic<Core*> _instance;
    static std::mutex m_;

  public:
    static Core *instance();
    int init(sparrow_options opts, bool allow_root);
    ~Core();

    // View lookup helpers
    SparrowView *find_view_by_handle(uint32_t handle);
    SparrowView *find_view_by_toplevel(const struct wlr_xdg_toplevel *toplevel);
    SparrowView *find_view_by_wlr_surface(const struct wlr_surface *surface);
    SparrowView *find_view_by_xdg_surface(const struct wlr_xdg_surface *xdg_surface);

    struct wl_display *wl_display = nullptr;
    struct wl_event_loop *wl_event_loop = nullptr;
    pthread_t main_thread_id    = 0;
    struct wlr_backend *backend = nullptr;
    struct wlr_session *session = nullptr;
    struct wlr_renderer *renderer   = nullptr;
    struct wlr_allocator *allocator = nullptr;
    struct wlr_presentation *presentation = nullptr;

    struct timespec last_render_time;

    EGLDisplay egl_display = nullptr;
    EGLContext egl_context = nullptr;

    CallableQueue callable_queue{};

    bool debug_damage = false;
    bool show_fps     = false;
    bool debug_protocol = false;
    bool debug_pacing   = false;

    enum BufferingMode
    {
        BUFFERING_DOUBLE = 0,
        BUFFERING_AUTO   = 1,
        BUFFERING_TRIPLE = 2,
    };

    BufferingMode buffering_mode = BUFFERING_DOUBLE;

    void dump_surface_tree() const;

    // Real client frame commit tracking (for true App FPS measurement)
    static constexpr int MAX_COMMIT_HISTORY = 128;
    uint64_t client_commit_timestamps[MAX_COMMIT_HISTORY] = {0};
    int client_commit_head  = 0;
    int client_commit_count = 0;

    struct wl_event_source *fps_decay_timer = nullptr;
    struct wl_event_source *callable_queue_event_source = nullptr;
    struct wl_event_source *sigint_event_source  = nullptr;
    struct wl_event_source *sigterm_event_source = nullptr;

    void record_client_commit(uint64_t now_us);

    double get_client_fps(uint64_t now_us) const
    {
        if (client_commit_count == 0)
        {
            return 0.0;
        }

        int prev_idx = (client_commit_head - 1 + MAX_COMMIT_HISTORY) % MAX_COMMIT_HISTORY;
        uint64_t last_commit = client_commit_timestamps[prev_idx];
        if ((now_us > last_commit) && ((now_us - last_commit) > 100000ULL))
        {
            return 0.0;
        }

        uint64_t window_us    = 500000ULL;
        uint64_t window_start = (now_us > window_us) ? (now_us - window_us) : 0;
        int recent = 0;
        for (int i = 0; i < client_commit_count; i++)
        {
            if (client_commit_timestamps[i] >= window_start)
            {
                recent++;
            }
        }

        return (double)recent * 2.0;
    }

    const char *wl_socket = nullptr;

    struct wlr_xdg_shell *xdg_shell = nullptr;
    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_xdg_popup;

    struct wlr_xdg_decoration_manager_v1 *decoration_manager = nullptr;
    struct wl_listener new_toplevel_decoration;

    // Legacy KDE server decoration protocol
    struct wlr_server_decoration_manager *legacy_decoration_manager = nullptr;
    struct wl_listener new_server_decoration;

    struct wlr_foreign_toplevel_manager_v1 *foreign_toplevel_manager   = nullptr;
    struct wlr_ext_foreign_toplevel_list_v1 *ext_foreign_toplevel_list = nullptr;

    // struct handle_map *views  = nullptr;
    struct handle_map *subsurfaces =
        nullptr; // Handle map for subsurface texture lookup
    struct handle_map *popups = nullptr; // Handle map for popup surfaces
    struct wl_list views_list;
    // uint32_t current_focused_view = 0;

    struct wlr_cursor *cursor = nullptr;
    struct wlr_xcursor_manager *cursor_mgr = nullptr;
    std::string current_xcursor_name =
        ""; // Current xcursor name for software rendering
    std::string flutter_cursor_name =
        "left_ptr"; // Current cursor name requested by Flutter
    struct wlr_texture *xcursor_texture =
        nullptr; // Cached xcursor texture for software rendering
    std::string xcursor_texture_name = ""; // Name of the cached xcursor texture
    struct wlr_surface *client_cursor_surface =
        nullptr; // Client-provided cursor surface
    struct wl_listener client_cursor_destroy;
    int32_t client_cursor_hotspot_x = 0;
    int32_t client_cursor_hotspot_y = 0;

    bool cursor_visible = true;

    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;
    struct wl_listener swipe_begin;
    struct wl_listener swipe_update;
    struct wl_listener swipe_end;
    struct wl_listener pinch_begin;
    struct wl_listener pinch_update;
    struct wl_listener pinch_end;
    struct wl_listener hold_begin;
    struct wl_listener hold_end;
    struct wl_listener cursor_touch_down;
    struct wl_listener cursor_touch_up;
    struct wl_listener cursor_touch_motion;
    struct wl_listener cursor_touch_frame;
    struct wl_listener cursor_touch_cancel;
    struct wl_listener request_cursor;
    struct wl_listener new_virtual_keyboard;
    struct wl_listener new_virtual_pointer;
    struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard_manager = nullptr;
    struct wlr_virtual_pointer_manager_v1 *virtual_pointer_manager   = nullptr;
    struct sparrow_input_state input = {};

    struct wl_listener new_input;
    struct wl_list keyboards;
    struct wl_list touchs;
    struct wl_list pointers;
    struct wlr_egl *egl = nullptr;

    struct wlr_seat *seat = nullptr;
    struct wl_listener request_set_selection;
    struct wl_listener request_set_primary_selection;
    struct wlr_cursor_shape_manager_v1 *cursor_shape_mgr = nullptr;
    struct wl_listener request_set_cursor_shape;
    struct wl_listener start_drag;
    struct wlr_idle_notifier_v1 *idle_notifier = nullptr;
    struct wlr_pointer_gestures_v1 *pointer_gestures = nullptr;
    struct wlr_relative_pointer_manager_v1 *relative_pointer_manager = nullptr;
    struct wlr_pointer_constraints_v1 *pointer_constraints = nullptr;
    struct wl_listener new_pointer_constraint;
    struct wlr_pointer_constraint_v1 *active_constraint = nullptr;

    // xdg-activation-v1 protocol support
    struct wlr_xdg_activation_v1 *xdg_activation = nullptr;
    struct wl_listener xdg_activation_request_activate;

    // Direct input mode - bypasses Flutter for low-latency gaming
    bool direct_input_mode = false;
    uint32_t direct_input_surface = 0; // Surface handle for direct input
    bool gesture_active = false; // Touchpad/touchscreen gesture currently in progress

    struct wlr_output_layout *output_layout = nullptr;
    struct wlr_scene *scene = nullptr;
    struct wlr_scene_output_layout *scene_output_layout = nullptr;
    struct wlr_scene_buffer *flutter_scene_buffer = nullptr;

    // Multi-output support
    struct wl_list outputs; // List of sparrow_output
    Output *vsync_output = nullptr; // Output that drives Flutter vsync
                                    // (highest refresh or user-selected)
    uint32_t next_output_id = 0; // Counter for generating unique output IDs
    int vsync_rate_limit    = 0; // 0 = unlimited, >0 = max Hz (for power saving)

    struct wl_listener new_output;
    struct wlr_output_manager_v1 *output_manager = nullptr;
    struct wl_listener output_manager_apply;
    struct wl_listener output_manager_test;
    struct wlr_output_power_manager_v1 *output_power_manager = nullptr;
    struct wl_listener output_power_manager_set_mode;

    FlutterEngineProcTable embedder_api = {};
    FlutterEngine engine = nullptr;
    FlutterCustomTaskRunners custom_task_runners = {};
    FlutterTaskRunnerDescription platform_task_runner = {};
    FlutterCompositor fl_compositor = {};

    pid_t platform_tid = 0;

    int platform_notify_fd = 0;
    struct wl_event_source *platform_notify_event_source = nullptr;
    struct wl_event_source *platform_timer_event_source  = nullptr;
    std::mutex platform_task_mutex;
    std::vector<sparrow_render_task> queued_platform_tasks;

    std::atomic<intptr_t> vsync_baton;
    struct sparrow_renderer sparrow_renderer = {};

    BinaryMessenger messenger{};
    std::unique_ptr<IncomingMessageDispatcher> message_dispatcher;
    std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> wlroots_channel;

    FlutterCustomTaskRunners task_runners{};

    // struct cg_seat* seat_manager = nullptr;

    std::unique_ptr<DebounceTime<std::string>> axis_debouncer = nullptr;
    bool axis_debouncer_ready = false;
    std::atomic<bool> force_render_all_views{false};
};

#endif
