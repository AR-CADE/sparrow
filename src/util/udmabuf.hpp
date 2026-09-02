#pragma once

#include <sparrow/nonstd/wlroots-full.hpp>

/**
 * Initialize udmabuf system. Opens /dev/udmabuf once.
 */
void sparrow_udmabuf_init();

/**
 * Shutdown udmabuf system and release global file descriptors.
 */
void sparrow_udmabuf_finish();

/**
 * Checks whether a surface's buffer is a wl_shm buffer and imports it via /dev/udmabuf as a hardware
 * zero-copy wlr_texture.
 *
 * Automatically cached on the wlr_buffer via wlr_addon.
 */
struct wlr_texture *sparrow_udmabuf_get_or_import_texture(struct wlr_surface *surface);

/**
 * Get texture for a wlr_surface, prioritizing zero-copy udmabuf hardware textures.
 */
struct wlr_texture *sparrow_surface_get_texture(struct wlr_surface *surface);
