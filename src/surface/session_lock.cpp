#include "session_lock.hpp"
#include "core.hpp"
#include "flutter/platform/text_input.hpp"
#include "output.hpp"
#include "surface/view.hpp"

struct SparrowIdleInhibitor
{
    struct wl_list link; // Core::idle_inhibitors
    struct wlr_idle_inhibitor_v1 *wlr_inhibitor = nullptr;
    struct wl_listener destroy;
};

struct SparrowLockSubsurface
{
    struct wl_list link; // SparrowSessionLockSurface::subsurfaces
    SparrowSessionLockSurface *lock_surface = nullptr;
    struct wlr_subsurface *wlr_subsurface   = nullptr;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener new_subsurface;
};

static void handle_lock_subsurface_commit(struct wl_listener *listener, void *data)
{
    (void)data;
    SparrowLockSubsurface *sub = wl_container_of(listener, sub, commit);
    if (sub && sub->lock_surface)
    {
        sparrow_damage_add_box(nullptr);
        if (sub->lock_surface->output && sub->lock_surface->output->wlr_output)
        {
            wlr_output_schedule_frame(sub->lock_surface->output->wlr_output);
        }
    }
}

static void handle_lock_subsurface_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    SparrowLockSubsurface *sub = wl_container_of(listener, sub, destroy);
    wl_list_remove(&sub->link);
    wl_list_remove(&sub->commit.link);
    wl_list_remove(&sub->destroy.link);
    wl_list_remove(&sub->new_subsurface.link);
    delete sub;
}

static void handle_lock_subsurface_new_subsurface(struct wl_listener *listener, void *data)
{
    SparrowLockSubsurface *sub = wl_container_of(listener, sub, new_subsurface);
    auto *child_sub = static_cast<struct wlr_subsurface*>(data);
    if (sub && sub->lock_surface)
    {
        sub->lock_surface->track_subsurface(child_sub);
    }
}

static void handle_lock_surface_new_subsurface(struct wl_listener *listener, void *data)
{
    SparrowSessionLockSurface *lock_surface =
        wl_container_of(listener, lock_surface, new_subsurface);
    auto *subsurface = static_cast<struct wlr_subsurface*>(data);
    lock_surface->track_subsurface(subsurface);
}

void SparrowSessionLockSurface::track_subsurface(struct wlr_subsurface *subsurface)
{
    if (!subsurface || !subsurface->surface)
    {
        return;
    }

    SparrowLockSubsurface *existing = nullptr;
    wl_list_for_each(existing, &subsurfaces, link)
    {
        if (existing->wlr_subsurface == subsurface)
        {
            return;
        }
    }

    auto *sub = new SparrowLockSubsurface();
    sub->lock_surface   = this;
    sub->wlr_subsurface = subsurface;

    sub->commit.notify = handle_lock_subsurface_commit;
    wl_signal_add(&subsurface->surface->events.commit, &sub->commit);

    sub->destroy.notify = handle_lock_subsurface_destroy;
    wl_signal_add(&subsurface->events.destroy, &sub->destroy);

    sub->new_subsurface.notify = handle_lock_subsurface_new_subsurface;
    wl_signal_add(&subsurface->surface->events.new_subsurface, &sub->new_subsurface);

    wl_list_insert(&subsurfaces, &sub->link);

    struct wlr_subsurface *child = nullptr;
    wl_list_for_each(child, &subsurface->surface->current.subsurfaces_below, current.link)
    {
        track_subsurface(child);
    }
    wl_list_for_each(child, &subsurface->surface->current.subsurfaces_above, current.link)
    {
        track_subsurface(child);
    }
}

static void handle_lock_surface_map(struct wl_listener *listener, void *data)
{
    (void)data;
    SparrowSessionLockSurface *lock_surface =
        wl_container_of(listener, lock_surface, map);
    wlr_log(WLR_INFO, "[SESSION-LOCK] Lock surface mapped for output '%s'",
        lock_surface->output && lock_surface->output->wlr_output ?
        lock_surface->output->wlr_output->name : "unknown");
    sparrow_session_lock_refocus();
}

static void handle_lock_surface_commit(struct wl_listener *listener, void *data)
{
    (void)data;
    SparrowSessionLockSurface *lock_surface =
        wl_container_of(listener, lock_surface, commit);
    lock_surface->committed = true;

    sparrow_damage_add_box(nullptr);
    if (lock_surface->output && lock_surface->output->wlr_output)
    {
        wlr_output_schedule_frame(lock_surface->output->wlr_output);
    }

    sparrow_session_lock_refocus();
    sparrow_session_lock_check_all_committed();
}

static void handle_lock_surface_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    SparrowSessionLockSurface *lock_surface =
        wl_container_of(listener, lock_surface, destroy);
    delete lock_surface;
}

SparrowSessionLockSurface::SparrowSessionLockSurface(
    struct wlr_session_lock_surface_v1 *surface, Output *out) :
    wlr_lock_surface(surface), output(out)
{
    Core *instance = Core::instance();
    surface->data = this;

    wl_list_init(&subsurfaces);

    destroy.notify = handle_lock_surface_destroy;
    wl_signal_add(&surface->events.destroy, &destroy);

    map.notify = handle_lock_surface_map;
    wl_signal_add(&surface->surface->events.map, &map);

    commit.notify = handle_lock_surface_commit;
    wl_signal_add(&surface->surface->events.commit, &commit);

    new_subsurface.notify = handle_lock_surface_new_subsurface;
    wl_signal_add(&surface->surface->events.new_subsurface, &new_subsurface);

    wl_list_insert(&instance->session_lock_surfaces, &link);

    struct wlr_subsurface *child = nullptr;
    wl_list_for_each(child, &surface->surface->current.subsurfaces_below, current.link)
    {
        track_subsurface(child);
    }
    wl_list_for_each(child, &surface->surface->current.subsurfaces_above, current.link)
    {
        track_subsurface(child);
    }

    configure();
}

SparrowSessionLockSurface::~SparrowSessionLockSurface()
{
    wl_list_remove(&destroy.link);
    wl_list_remove(&map.link);
    wl_list_remove(&commit.link);
    wl_list_remove(&new_subsurface.link);
    wl_list_remove(&link);

    SparrowLockSubsurface *sub = nullptr;
    SparrowLockSubsurface *tmp = nullptr;
    wl_list_for_each_safe(sub, tmp, &subsurfaces, link)
    {
        wl_list_remove(&sub->link);
        wl_list_remove(&sub->commit.link);
        wl_list_remove(&sub->destroy.link);
        wl_list_remove(&sub->new_subsurface.link);
        delete sub;
    }

    if (wlr_lock_surface && (wlr_lock_surface->data == this))
    {
        wlr_lock_surface->data = nullptr;
    }
}

void SparrowSessionLockSurface::configure()
{
    if (!output || !output->wlr_output || !wlr_lock_surface)
    {
        return;
    }

    int eff_w = 0, eff_h = 0;
    wlr_output_effective_resolution(output->wlr_output, &eff_w, &eff_h);
    wlr_log(WLR_INFO,
        "[SESSION-LOCK] Configuring lock surface for output '%s' to %dx%d (transform=%d)",
        output->wlr_output->name, eff_w, eff_h, output->wlr_output->transform);
    wlr_session_lock_surface_v1_configure(wlr_lock_surface, eff_w, eff_h);
}

void sparrow_session_lock_configure_output(Output *output)
{
    if (!output)
    {
        return;
    }

    SparrowSessionLockSurface *surf =
        sparrow_session_lock_surface_for_output(output);
    if (surf)
    {
        surf->configure();
        wlr_log(WLR_INFO,
            "[SESSION-LOCK] Reconfigured lock surface for output '%s'",
            output->wlr_output ? output->wlr_output->name : "unknown");
    }
}

void sparrow_session_lock_configure_all()
{
    Core *instance = Core::instance();
    if (!instance)
    {
        return;
    }

    SparrowSessionLockSurface *surf = nullptr;
    wl_list_for_each(surf, &instance->session_lock_surfaces, link)
    {
        surf->configure();
    }
}

void sparrow_session_lock_check_all_committed()
{
    Core *instance = Core::instance();
    if (!instance || !instance->current_session_lock)
    {
        return;
    }

    if (instance->session_locked_sent)
    {
        return;
    }

    bool all_committed = true;
    Output *out = nullptr;
    wl_list_for_each(out, &instance->outputs, link)
    {
        if (!out->wlr_output || !out->wlr_output->enabled)
        {
            continue;
        }

        SparrowSessionLockSurface *surf = sparrow_session_lock_surface_for_output(out);
        if (!surf || !surf->committed)
        {
            all_committed = false;
            break;
        }
    }

    if (all_committed)
    {
        instance->session_locked_sent = true;
        wlr_session_lock_v1_send_locked(instance->current_session_lock);
        wlr_log(WLR_INFO, "[SESSION-LOCK] All outputs locked, sent locked event to client");
        sparrow_session_lock_refocus();
        sparrow_damage_add_box(nullptr);
    }
}

static void handle_session_lock_new_surface(struct wl_listener *listener, void *data)
{
    (void)listener;
    auto *wlr_lock_surface = static_cast<struct wlr_session_lock_surface_v1*>(data);
    Core *instance = Core::instance();

    Output *target_out = nullptr;
    Output *out = nullptr;
    wl_list_for_each(out, &instance->outputs, link)
    {
        if (out->wlr_output == wlr_lock_surface->output)
        {
            target_out = out;
            break;
        }
    }

    if (!target_out)
    {
        wlr_log(WLR_ERROR,
            "[SESSION-LOCK] New lock surface for unknown output %s",
            wlr_lock_surface->output ? wlr_lock_surface->output->name : "null");
        return;
    }

    auto *lock_surf = new SparrowSessionLockSurface(wlr_lock_surface, target_out);
    sparrow_session_lock_focus_surface(wlr_lock_surface->surface);
    wlr_log(WLR_INFO,
        "[SESSION-LOCK] Created lock surface for output '%s' (%p)",
        target_out->wlr_output->name, (void*)lock_surf);
}

static void handle_session_lock_unlock(struct wl_listener *listener, void *data)
{
    (void)listener;
    (void)data;
    Core *instance = Core::instance();
    wlr_log(WLR_INFO, "[SESSION-LOCK] Session unlocked by client");

    instance->current_session_lock = nullptr;
    instance->session_locked_sent  = false;

    if (instance->seat)
    {
        wlr_seat_keyboard_clear_focus(instance->seat);
        if (!wl_list_empty(&instance->views_list))
        {
            SparrowView *view = wl_container_of(instance->views_list.next, view, link);
            if (view && view->xdg_surface && view->xdg_surface->surface &&
                view->xdg_surface->surface->mapped)
            {
                sparrow_view_focus(view);
            }
        }
    }

    sparrow_damage_add_box(nullptr);
    sparrow_update_idle_inhibited();
}

static void handle_session_lock_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    Core *instance = Core::instance();

    wl_list_remove(&instance->session_lock_new_surface.link);
    wl_list_remove(&instance->session_lock_unlock.link);
    wl_list_remove(&instance->session_lock_destroy.link);

    if (instance->current_session_lock)
    {
        wlr_log(WLR_INFO, "[SESSION-LOCK] Session lock destroyed");
        instance->current_session_lock = nullptr;
        instance->session_locked_sent  = false;
        if (instance->seat)
        {
            wlr_seat_keyboard_clear_focus(instance->seat);
            if (!wl_list_empty(&instance->views_list))
            {
                SparrowView *view = wl_container_of(instance->views_list.next, view, link);
                if (view && view->xdg_surface && view->xdg_surface->surface &&
                    view->xdg_surface->surface->mapped)
                {
                    sparrow_view_focus(view);
                }
            }
        }

        sparrow_damage_add_box(nullptr);
        sparrow_update_idle_inhibited();
    }
}

static void handle_new_session_lock(struct wl_listener *listener, void *data)
{
    (void)listener;
    auto *lock     = static_cast<struct wlr_session_lock_v1*>(data);
    Core *instance = Core::instance();

    if (instance->current_session_lock != nullptr)
    {
        wlr_log(WLR_ERROR, "[SESSION-LOCK] Rejecting concurrent session lock request");
        wlr_session_lock_v1_destroy(lock);
        return;
    }

    instance->current_session_lock = lock;
    instance->session_locked_sent  = false;
    wlr_log(WLR_INFO, "[SESSION-LOCK] Session lock established");

    // Deactivate all active application views and clear seat focus immediately
    SparrowView *v = nullptr;
    wl_list_for_each(v, &instance->views_list, link)
    {
        if (v->activated)
        {
            v->activated = false;
            if (v->toplevel)
            {
                wlr_xdg_toplevel_set_activated(v->toplevel, false);
            }

            if (v->foreign_toplevel)
            {
                wlr_foreign_toplevel_handle_v1_set_activated(v->foreign_toplevel, false);
            }
        }
    }

    if (instance->seat)
    {
        wlr_seat_pointer_clear_focus(instance->seat);
        wlr_seat_keyboard_notify_clear_focus(instance->seat);
    }

    sparrow_text_input_stop_repeat(0);

    instance->session_lock_new_surface.notify = handle_session_lock_new_surface;
    wl_signal_add(&lock->events.new_surface, &instance->session_lock_new_surface);

    instance->session_lock_unlock.notify = handle_session_lock_unlock;
    wl_signal_add(&lock->events.unlock, &instance->session_lock_unlock);

    instance->session_lock_destroy.notify = handle_session_lock_destroy;
    wl_signal_add(&lock->events.destroy, &instance->session_lock_destroy);

    if (wl_list_empty(&instance->outputs))
    {
        instance->session_locked_sent = true;
        wlr_session_lock_v1_send_locked(lock);
    }

    sparrow_damage_add_box(nullptr);
    sparrow_update_idle_inhibited();
}

void sparrow_session_lock_init(Core *core)
{
    core->session_lock_manager =
        wlr_session_lock_manager_v1_create(core->wl_display);
    if (!core->session_lock_manager)
    {
        wlr_log(WLR_ERROR, "Failed to create wlr_session_lock_manager_v1");
        return;
    }

    core->new_session_lock.notify = handle_new_session_lock;
    wl_signal_add(&core->session_lock_manager->events.new_lock,
        &core->new_session_lock);

    wlr_log(WLR_INFO, "ext-session-lock-v1 protocol initialized");
}

bool sparrow_is_session_locked()
{
    Core *instance = Core::instance();
    return instance && (instance->current_session_lock != nullptr);
}

struct wlr_surface *sparrow_session_lock_surface_at(double lx, double ly,
    double *sx, double *sy)
{
    Core *instance = Core::instance();
    if (!instance || !instance->current_session_lock || !instance->output_layout)
    {
        return nullptr;
    }

    struct wlr_output *wlr_out =
        wlr_output_layout_output_at(instance->output_layout, lx, ly);
    if (!wlr_out)
    {
        return nullptr;
    }

    Output *target_out = nullptr;
    Output *out = nullptr;
    wl_list_for_each(out, &instance->outputs, link)
    {
        if (out->wlr_output == wlr_out)
        {
            target_out = out;
            break;
        }
    }

    if (!target_out)
    {
        return nullptr;
    }

    SparrowSessionLockSurface *surf =
        sparrow_session_lock_surface_for_output(target_out);
    if (!surf || !surf->wlr_lock_surface || !surf->wlr_lock_surface->surface)
    {
        return nullptr;
    }

    struct wlr_box box;
    wlr_output_layout_get_box(instance->output_layout, wlr_out, &box);
    double local_x = lx - box.x;
    double local_y = ly - box.y;

    return wlr_surface_surface_at(surf->wlr_lock_surface->surface,
        local_x, local_y, sx, sy);
}

SparrowSessionLockSurface *sparrow_session_lock_surface_for_output(Output *output)
{
    Core *instance = Core::instance();
    if (!instance)
    {
        return nullptr;
    }

    SparrowSessionLockSurface *surf = nullptr;
    wl_list_for_each(surf, &instance->session_lock_surfaces, link)
    {
        if (surf->output == output)
        {
            return surf;
        }
    }

    return nullptr;
}

bool sparrow_is_lock_surface(const struct wlr_surface *surface)
{
    if (!surface)
    {
        return false;
    }

    Core *instance = Core::instance();
    if (!instance || !instance->current_session_lock)
    {
        return false;
    }

    const struct wlr_surface *root = wlr_surface_get_root_surface(
        const_cast<struct wlr_surface*>(surface));
    SparrowSessionLockSurface *surf = nullptr;
    wl_list_for_each(surf, &instance->session_lock_surfaces, link)
    {
        if (surf->wlr_lock_surface &&
            ((surf->wlr_lock_surface->surface == root) ||
             (surf->wlr_lock_surface->surface == surface)))
        {
            return true;
        }
    }

    return false;
}

void sparrow_session_lock_refocus()
{
    Core *instance = Core::instance();
    if (!instance || !instance->current_session_lock || !instance->seat)
    {
        return;
    }

    struct wlr_surface *target_surface = nullptr;
    double sx = 0, sy = 0;
    target_surface = sparrow_session_lock_surface_at(
        instance->cursor->x, instance->cursor->y, &sx, &sy);

    if (!target_surface)
    {
        SparrowSessionLockSurface *surf = nullptr;
        wl_list_for_each(surf, &instance->session_lock_surfaces, link)
        {
            if (surf->wlr_lock_surface && surf->wlr_lock_surface->surface &&
                surf->wlr_lock_surface->surface->mapped)
            {
                target_surface = surf->wlr_lock_surface->surface;
                break;
            }
        }
    }

    if (!target_surface)
    {
        SparrowSessionLockSurface *surf = nullptr;
        wl_list_for_each(surf, &instance->session_lock_surfaces, link)
        {
            if (surf->wlr_lock_surface && surf->wlr_lock_surface->surface)
            {
                target_surface = surf->wlr_lock_surface->surface;
                break;
            }
        }
    }

    if (!target_surface)
    {
        return;
    }

    if (instance->seat->keyboard_state.focused_surface != target_surface)
    {
        sparrow_session_lock_focus_surface(target_surface);
    }
}

void sparrow_session_lock_focus_surface(struct wlr_surface *surface)
{
    Core *instance = Core::instance();
    if (!instance || !instance->seat || !surface)
    {
        return;
    }

    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(instance->seat);
    if (keyboard != nullptr)
    {
        wlr_seat_keyboard_notify_enter(instance->seat, surface, keyboard->keycodes,
            keyboard->num_keycodes, &keyboard->modifiers);
    } else
    {
        struct wlr_keyboard_modifiers modifiers = {};
        wlr_seat_keyboard_notify_enter(instance->seat, surface, nullptr, 0, &modifiers);
    }
}

static void handle_idle_inhibitor_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    SparrowIdleInhibitor *inhibitor =
        wl_container_of(listener, inhibitor, destroy);
    wl_list_remove(&inhibitor->destroy.link);
    wl_list_remove(&inhibitor->link);
    delete inhibitor;

    wlr_log(WLR_INFO, "[IDLE-INHIBIT] Idle inhibitor destroyed");
    sparrow_update_idle_inhibited();
}

static void handle_new_idle_inhibitor(struct wl_listener *listener, void *data)
{
    (void)listener;
    auto *wlr_inhibitor = static_cast<struct wlr_idle_inhibitor_v1*>(data);
    Core *instance = Core::instance();

    auto *inhibitor = new SparrowIdleInhibitor();
    inhibitor->wlr_inhibitor  = wlr_inhibitor;
    inhibitor->destroy.notify = handle_idle_inhibitor_destroy;
    wl_signal_add(&wlr_inhibitor->events.destroy, &inhibitor->destroy);

    wl_list_insert(&instance->idle_inhibitors, &inhibitor->link);

    wlr_log(WLR_INFO,
        "[IDLE-INHIBIT] Registered idle inhibitor for surface %p",
        (void*)wlr_inhibitor->surface);
    sparrow_update_idle_inhibited();
}

void sparrow_idle_inhibit_init(Core *core)
{
    core->idle_inhibit_manager = wlr_idle_inhibit_v1_create(core->wl_display);
    if (!core->idle_inhibit_manager)
    {
        wlr_log(WLR_ERROR, "Failed to create wlr_idle_inhibit_manager_v1");
        return;
    }

    core->new_idle_inhibitor.notify = handle_new_idle_inhibitor;
    wl_signal_add(&core->idle_inhibit_manager->events.new_inhibitor,
        &core->new_idle_inhibitor);

    wlr_log(WLR_INFO, "wlr-idle-inhibit-v1 protocol initialized");
}

void sparrow_update_idle_inhibited()
{
    Core *instance = Core::instance();
    if (!instance || !instance->idle_notifier)
    {
        return;
    }

    if (sparrow_is_session_locked())
    {
        // When session is locked, idle inhibition is suspended so screensaver / DPMS can trigger
        wlr_idle_notifier_v1_set_inhibited(instance->idle_notifier, false);
        return;
    }

    bool is_inhibited = !wl_list_empty(&instance->idle_inhibitors);
    wlr_idle_notifier_v1_set_inhibited(instance->idle_notifier, is_inhibited);
}

void sparrow_session_lock_finish(Core *core)
{
    if (!core)
    {
        return;
    }

    if (core->new_session_lock.link.next && core->new_session_lock.link.prev &&
        (core->new_session_lock.link.next != &core->new_session_lock.link))
    {
        wl_list_remove(&core->new_session_lock.link);
        wl_list_init(&core->new_session_lock.link);
    }

    if (core->session_lock_surfaces.next && core->session_lock_surfaces.prev)
    {
        SparrowSessionLockSurface *surf = nullptr, *tmp = nullptr;
        wl_list_for_each_safe(surf, tmp, &core->session_lock_surfaces, link)
        {
            delete surf;
        }
        wl_list_init(&core->session_lock_surfaces);
    }
}

void sparrow_idle_inhibit_finish(Core *core)
{
    if (!core)
    {
        return;
    }

    if (core->new_idle_inhibitor.link.next && core->new_idle_inhibitor.link.prev &&
        (core->new_idle_inhibitor.link.next != &core->new_idle_inhibitor.link))
    {
        wl_list_remove(&core->new_idle_inhibitor.link);
        wl_list_init(&core->new_idle_inhibitor.link);
    }

    if (core->idle_inhibitors.next && core->idle_inhibitors.prev)
    {
        SparrowIdleInhibitor *inh = nullptr, *tmp = nullptr;
        wl_list_for_each_safe(inh, tmp, &core->idle_inhibitors, link)
        {
            wl_list_remove(&inh->destroy.link);
            wl_list_remove(&inh->link);
            delete inh;
        }
        wl_list_init(&core->idle_inhibitors);
    }
}
