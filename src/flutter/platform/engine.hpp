#ifndef ENGINE_H
#define ENGINE_H

#include "flutter_embedder.h"

// #define eglGetProcAddr eglGetProcAddress
// #define __glintercept_log(...) wlr_log(WLR_INFO, __VA_ARGS__)
// #include "gl_intercept_debug.h"
#define GL_ASSERT_ERROR(instance) \
        do { \
            GLenum err = instance->glGetError(); \
            if (err != GL_NO_ERROR) { \
                wlr_log(WLR_ERROR, "GL ERROR: %d", err); \
            } \
        } while (0)

bool engine_cb_renderer_make_current(void *user_data);
bool engine_cb_renderer_clear_current(void *user_data);
bool engine_cb_renderer_make_resource_current(void *user_data);
void * engine_cb_renderer_gl_proc_resolve(void *user_data, const char *name);
bool engine_cb_external_texture(void *user_data, int64_t texture_id, size_t width, size_t height,
    FlutterOpenGLTexture *texture_out);
uint32_t engine_cb_renderer_fbo(void *user_data, const FlutterFrameInfo *frame_info);
void engine_cb_platform_message(const FlutterPlatformMessage *engine_message, void *user_data);
void engine_cb_log_message(const char *tag, const char *message, void *user_data);
bool engine_cb_renderer_present(void *user_data, const FlutterPresentInfo *present_info);
void engine_dispose(FlutterEngine engine, FlutterEngineAOTData aot_data);
void sparrow_engine_init_channels();

#endif
