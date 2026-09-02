#include "keyboard.hpp"
#include "seat.hpp"
#include <core.hpp>

#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

void handle_new_virtual_keyboard(struct wl_listener *listener, void *data)
{
    Core *instance = Core::instance();
    struct wlr_virtual_keyboard_v1 *virtual_keyboard =
        static_cast<struct wlr_virtual_keyboard_v1*>(data);

    struct sparrow_keyboard *keyboard = new sparrow_keyboard();
    if (!keyboard)
    {
        wlr_log(WLR_ERROR,
            "Failed to allocate sparrow_keyboard for virtual keyboard");
        return;
    }

    keyboard->keyboard = &virtual_keyboard->keyboard;
    keyboard->compose_state = nullptr;

    keyboard->modifiers.notify = keyboard_handle_modifiers;
    wl_signal_add(&virtual_keyboard->keyboard.events.modifiers,
        &keyboard->modifiers);
    keyboard->key.notify = keyboard_handle_key;
    wl_signal_add(&virtual_keyboard->keyboard.events.key, &keyboard->key);
    keyboard->destroy.notify = keyboard_destroy;
    wl_signal_add(&virtual_keyboard->keyboard.base.events.destroy,
        &keyboard->destroy);

    wlr_seat_set_keyboard(instance->seat, &virtual_keyboard->keyboard);

    wl_list_insert(&instance->keyboards, &keyboard->link);
    sparrow_seat_update_capabilities();
    wlr_log(WLR_INFO, "New virtual keyboard attached and registered");
}

void keyboard_handle_modifiers(struct wl_listener *listener, void *data)
{
    /* This event is raised when a modifier key, such as shift or alt, is pressed. We simply communicate this
     * to the client. */
    struct sparrow_keyboard *keyboard =
        wl_container_of(listener, keyboard, modifiers);
    Core *instance = Core::instance();

    /*
     * A seat can only have one keyboard, but this is a limitation of the Wayland protocol - not wlroots. We
     * assign all connected keyboards to the same seat. You can swap out the underlying wlr_keyboard like this
     * and wlr_seat handles this transparently.
     */
    wlr_seat_set_keyboard(instance->seat, keyboard->keyboard);
    /* Send modifiers to the client. */
    wlr_seat_keyboard_notify_modifiers(instance->seat,
        &keyboard->keyboard->modifiers);
}

void keyboard_handle_key(struct wl_listener *listener, void *data)
{
    struct sparrow_keyboard *keyboard    = wl_container_of(listener, keyboard, key);
    struct wlr_keyboard_key_event *event =
        static_cast<wlr_keyboard_key_event*>(data);
    Core *instance = Core::instance();

    wlr_seat_set_keyboard(instance->seat, keyboard->keyboard);

    if ((event->state == WL_KEYBOARD_KEY_STATE_PRESSED) && keyboard->keyboard &&
        keyboard->keyboard->xkb_state)
    {
        const xkb_keysym_t sym = xkb_state_key_get_one_sym(
            keyboard->keyboard->xkb_state, event->keycode + 8);
        if (sym == XKB_KEY_F8)
        {
            Output *out = instance->vsync_output ? instance->vsync_output :
                sparrow_get_first_output();
            if (out && out->wlr_output)
            {
                struct wlr_output_state state;
                wlr_output_state_init(&state);
                bool new_enabled = !out->wlr_output->enabled;
                wlr_output_state_set_enabled(&state, new_enabled);
                if (!wlr_output_commit_state(out->wlr_output, &state))
                {
                    wlr_log(WLR_ERROR,
                        "[DPMS] Failed to commit power toggle for output '%s'",
                        out->wlr_output->name);
                } else
                {
                    wlr_log(WLR_INFO, "[DPMS] Output '%s' power toggled via F8: %s",
                        out->wlr_output->name, new_enabled ? "ON" : "OFF");
                    if (new_enabled)
                    {
                        sparrow_select_highest_refresh_output();
                        sparrow_damage_add_box(nullptr);
                        wlr_output_schedule_frame(out->wlr_output);
                    }
                }

                wlr_output_state_finish(&state);
            }

            return;
        }

        if (sym == XKB_KEY_F9)
        {
            instance->buffering_mode = static_cast<Core::BufferingMode>(
                (static_cast<int>(instance->buffering_mode) + 1) % 3);
            const char *mode_str =
                (instance->buffering_mode == Core::BUFFERING_DOUBLE) ?
                "DOUBLE BUFFERING (DB)" :
                (instance->buffering_mode == Core::BUFFERING_AUTO) ?
                "DYNAMIC TRIPLE BUFFERING (AUTO)" :
                "FORCED TRIPLE BUFFERING (TB:ON)";
            wlr_log(WLR_INFO, "[BUFFERING] Mode changed via F9: %s", mode_str);
            sparrow_damage_add_box(nullptr);
            return;
        }

        if (sym == XKB_KEY_F10)
        {
            instance->dump_surface_tree();
            return;
        }

        if (sym == XKB_KEY_F11)
        {
            instance->show_fps = !instance->show_fps;
            instance->client_commit_count = 0;
            instance->client_commit_head  = 0;
            if (!instance->show_fps && instance->fps_decay_timer)
            {
                wl_event_source_timer_update(instance->fps_decay_timer, 0);
            }

            wlr_log(WLR_INFO, "FPS OSD monitor toggled: %s",
                instance->show_fps ? "ENABLED" : "DISABLED");
            sparrow_damage_add_box(nullptr);
            return;
        }

        if (sym == XKB_KEY_F12)
        {
            instance->debug_damage = !instance->debug_damage;
            wlr_log(WLR_INFO, "Damage visualization debug mode toggled: %s",
                instance->debug_damage ? "ENABLED" : "DISABLED");
            sparrow_damage_add_box(nullptr);
            return;
        }
    }

    wlr_seat_keyboard_notify_key(instance->seat, event->time_msec, event->keycode,
        event->state);
}

void keyboard_destroy(struct wl_listener *listener, void *data)
{
    struct sparrow_keyboard *keyboard =
        wl_container_of(listener, keyboard, destroy);
    if (keyboard->compose_state != nullptr)
    {
        xkb_compose_state_unref(keyboard->compose_state);
        keyboard->compose_state = nullptr;
    }

    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->destroy.link);
    wl_list_remove(&keyboard->link);
    delete keyboard;

    sparrow_seat_update_capabilities();
}

void server_new_keyboard(struct wlr_input_device *device)
{
    struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

    // wlr_keyboard_init(wlr_keyboard, &keyboard_impl, "stipc_keyboard");

    struct sparrow_keyboard *keyboard = new sparrow_keyboard();
    if (!keyboard)
    {
        wlr_log(WLR_ERROR, "Failed to allocate sparrow_keyboard");
        exit(1);
    }

    Core *instance = Core::instance();
    keyboard->keyboard = wlr_keyboard;

    // Prepare XKB keymap and asing to keyboard, default layout is "us"
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    if (!context)
    {
        wlr_log(WLR_ERROR, "Failed to create XKB context");
        exit(1);
    }

    struct xkb_rule_names rules = {};
    rules.rules   = getenv("XKB_DEFAULT_RULES");
    rules.model   = getenv("XKB_DEFAULT_MODEL");
    rules.layout  = getenv("XKB_DEFAULT_LAYOUT");
    rules.variant = getenv("XKB_DEFAULT_VARIANT");
    rules.options = getenv("XKB_DEFAULT_OPTIONS");
    struct xkb_keymap *keymap =
        xkb_keymap_new_from_names(context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);

    struct xkb_compose_table *compose_table = xkb_compose_table_new_from_locale(
        context, setlocale(LC_CTYPE, nullptr), XKB_COMPOSE_COMPILE_NO_FLAGS);
    if (compose_table != nullptr)
    {
        keyboard->compose_state =
            xkb_compose_state_new(compose_table, XKB_COMPOSE_STATE_NO_FLAGS);
        xkb_compose_table_unref(compose_table);
    } else
    {
        keyboard->compose_state = nullptr;
        wlr_log(WLR_ERROR, "Could not create new XKB compose table.\n");
    }

    wlr_keyboard_set_keymap(wlr_keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 300);

    keyboard->modifiers.notify = keyboard_handle_modifiers;
    wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
    keyboard->key.notify = keyboard_handle_key;
    wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
    keyboard->destroy.notify = keyboard_destroy;
    wl_signal_add(&wlr_keyboard->base.events.destroy, &keyboard->destroy);

    wlr_seat_set_keyboard(instance->seat, wlr_keyboard);

    // add keyboard to list of keyboards
    wl_list_insert(&instance->keyboards, &keyboard->link);
}
