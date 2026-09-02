#pragma once

#include <functional>
#include <sparrow/nonstd/wlroots-full.hpp>

/**
 * Initialize the eventfd-based Wayland event loop dispatcher.
 */
void sparrow_dispatcher_init(struct wl_display *display);

/**
 * Clean up the dispatcher.
 */
void sparrow_dispatcher_finish();

/**
 * Safely dispatch a task to be executed on the Wayland / wlroots main thread. Thread-safe: can be called from
 * any thread (Flutter UI, Isolate, worker threads).
 */
void sparrow_dispatch_to_wayland(std::function<void()> task);
