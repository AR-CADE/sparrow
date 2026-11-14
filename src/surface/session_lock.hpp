#ifndef SPARROW_SESSION_LOCK_HPP
#define SPARROW_SESSION_LOCK_HPP

#include <sparrow/nonstd/wlroots-full.hpp>

class Output;
class Core;

class SparrowSessionLockSurface
{
  public:
    struct wl_list link; // Core::session_lock_surfaces
    struct wlr_session_lock_surface_v1 *wlr_lock_surface = nullptr;
    Output *output = nullptr;
    bool committed = false;

    struct wl_listener commit;
    struct wl_listener map;
    struct wl_listener destroy;
    struct wl_listener new_subsurface;
    struct wl_list subsurfaces; // SparrowLockSubsurface::link

    SparrowSessionLockSurface(struct wlr_session_lock_surface_v1 *surface, Output *out);
    ~SparrowSessionLockSurface();

    void configure();
    void track_subsurface(struct wlr_subsurface *subsurface);
};

void sparrow_session_lock_init(Core *core);
void sparrow_session_lock_finish(Core *core);
bool sparrow_is_session_locked();
bool sparrow_is_lock_surface(const struct wlr_surface *surface);
void sparrow_session_lock_refocus();
struct wlr_surface *sparrow_session_lock_surface_at(double lx, double ly, double *sx, double *sy);
SparrowSessionLockSurface *sparrow_session_lock_surface_for_output(Output *output);
void sparrow_session_lock_focus_surface(struct wlr_surface *surface);
void sparrow_session_lock_check_all_committed();
void sparrow_session_lock_configure_output(Output *output);
void sparrow_session_lock_configure_all();

void sparrow_idle_inhibit_init(Core *core);
void sparrow_idle_inhibit_finish(Core *core);
void sparrow_update_idle_inhibited();

#endif // SPARROW_SESSION_LOCK_HPP
