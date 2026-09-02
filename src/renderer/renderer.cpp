#include "renderer.hpp"
#include "core.hpp"
#include "flutter_embedder.h"
#include "shaders.hpp"
#include "surface/surface.hpp"
#include "surface/view.hpp"
#include "util/trace.hpp"
#include "util/udmabuf.hpp"
#include <cstdio>

#ifdef USE_DMABUF
    #include <libdrm/drm_fourcc.h>
#endif

#include <sparrow/nonstd/wlroots-full.hpp>

#define GL_BGRA_EXT 0x80E1
#ifndef GL_RGBA8
    #define GL_RGBA8 0x8058
#endif
#ifndef GL_DEPTH24_STENCIL8
    #define GL_DEPTH24_STENCIL8 0x88F0
#endif
#ifndef GL_DEPTH_STENCIL_ATTACHMENT
    #define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#endif

static const GLfloat texcoords[8] = {
    1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f,
};

static void get_texcoords_for_transform(enum wl_output_transform tr,
    GLfloat uvs[8])
{
    switch (tr)
    {
      case WL_OUTPUT_TRANSFORM_90:
        uvs[0] = 1.0f;
        uvs[1] = 0.0f;
        uvs[2] = 1.0f;
        uvs[3] = 1.0f;
        uvs[4] = 0.0f;
        uvs[5] = 0.0f;
        uvs[6] = 0.0f;
        uvs[7] = 1.0f;
        break;

      case WL_OUTPUT_TRANSFORM_180:
        uvs[0] = 0.0f;
        uvs[1] = 0.0f;
        uvs[2] = 1.0f;
        uvs[3] = 0.0f;
        uvs[4] = 0.0f;
        uvs[5] = 1.0f;
        uvs[6] = 1.0f;
        uvs[7] = 1.0f;
        break;

      case WL_OUTPUT_TRANSFORM_270:
        uvs[0] = 0.0f;
        uvs[1] = 1.0f;
        uvs[2] = 0.0f;
        uvs[3] = 0.0f;
        uvs[4] = 1.0f;
        uvs[5] = 1.0f;
        uvs[6] = 1.0f;
        uvs[7] = 0.0f;
        break;

      case WL_OUTPUT_TRANSFORM_FLIPPED:
        uvs[0] = 0.0f;
        uvs[1] = 1.0f;
        uvs[2] = 1.0f;
        uvs[3] = 1.0f;
        uvs[4] = 0.0f;
        uvs[5] = 0.0f;
        uvs[6] = 1.0f;
        uvs[7] = 0.0f;
        break;

      case WL_OUTPUT_TRANSFORM_FLIPPED_90:
        uvs[0] = 0.0f;
        uvs[1] = 0.0f;
        uvs[2] = 0.0f;
        uvs[3] = 1.0f;
        uvs[4] = 1.0f;
        uvs[5] = 0.0f;
        uvs[6] = 1.0f;
        uvs[7] = 1.0f;
        break;

      case WL_OUTPUT_TRANSFORM_FLIPPED_180:
        uvs[0] = 1.0f;
        uvs[1] = 0.0f;
        uvs[2] = 0.0f;
        uvs[3] = 0.0f;
        uvs[4] = 1.0f;
        uvs[5] = 1.0f;
        uvs[6] = 0.0f;
        uvs[7] = 1.0f;
        break;

      case WL_OUTPUT_TRANSFORM_FLIPPED_270:
        uvs[0] = 1.0f;
        uvs[1] = 1.0f;
        uvs[2] = 1.0f;
        uvs[3] = 0.0f;
        uvs[4] = 0.0f;
        uvs[5] = 1.0f;
        uvs[6] = 0.0f;
        uvs[7] = 0.0f;
        break;

      default: // NORMAL
        uvs[0] = 1.0f;
        uvs[1] = 1.0f;
        uvs[2] = 0.0f;
        uvs[3] = 1.0f;
        uvs[4] = 1.0f;
        uvs[5] = 0.0f;
        uvs[6] = 0.0f;
        uvs[7] = 0.0f;
        break;
    }
}

static void texture_destruction_callback(void *user_data)
{
}

static struct sparrow_renderer_page_texture *page_get_texture(size_t width, size_t height, bool make_fbo)
{
    Core *instance = Core::instance();

    if (instance == nullptr)
    {
        return nullptr;
    }

    struct sparrow_renderer *renderer = &instance->sparrow_renderer;
#ifndef USE_GLES32
    const struct gl_fns *fns = &renderer->fns;
#endif
    struct sparrow_renderer_page *page = &renderer->pages[renderer->current_page];

    struct sparrow_renderer_page_texture *page_texture;

    {
        GLuint err;
        GLuint fbo = 0;
#ifdef USE_GLES32
        if (make_fbo)
        {
            glGenFramebuffers(1, &fbo);
            // wlr_log(WLR_DEBUG, "fbo: %d", fbo);

            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        }

        GLuint tex = 0;
        glGenTextures(1, &tex);
        // wlr_log(WLR_DEBUG, "tex: %d", tex);

        glBindTexture(GL_TEXTURE_2D, tex);
    #ifdef ENABLE_IMPELLER
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
            GL_UNSIGNED_BYTE, nullptr);
    #else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA_EXT, width, height, 0, GL_BGRA_EXT,
            GL_UNSIGNED_BYTE, nullptr);
    #endif
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        GLuint rbo = 0;
        if (make_fbo)
        {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D, tex, 0);

            const GLenum drawBuffers[1] = {GL_COLOR_ATTACHMENT0};
            glDrawBuffers(1, drawBuffers);

            glGenRenderbuffers(1, &rbo);
            glBindRenderbuffer(GL_RENDERBUFFER, rbo);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width,
                height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                GL_RENDERBUFFER, rbo);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        glBindTexture(GL_TEXTURE_2D, 0);

        err = glGetError();
#else
        if (make_fbo)
        {
            fns->glGenFramebuffers(1, &fbo);
            // wlr_log(WLR_DEBUG, "fbo: %d", fbo);

            fns->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        }

        GLuint tex = 0;
        fns->glGenTextures(1, &tex);
        // wlr_log(WLR_DEBUG, "tex: %d", tex);

        fns->glBindTexture(GL_TEXTURE_2D, tex);
    #ifdef ENABLE_IMPELLER
        fns->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
            GL_UNSIGNED_BYTE, nullptr);
    #else
        fns->glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA_EXT, width, height, 0,
            GL_BGRA_EXT, GL_UNSIGNED_BYTE, nullptr);
    #endif
        fns->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        fns->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        if (make_fbo)
        {
            fns->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D, tex, 0);

            const GLenum drawBuffers[1] = {GL_COLOR_ATTACHMENT0};
            fns->glDrawBuffers(1, drawBuffers);

            fns->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        fns->glBindTexture(GL_TEXTURE_2D, 0);

        err = fns->glGetError();
#endif

        if (err != 0)
        {
            wlr_log(WLR_ERROR, "GL ERROR %d", err);
        }

        page_texture = new sparrow_renderer_page_texture();
        if (!page_texture)
        {
            wlr_log(WLR_ERROR, "Failed to allocate sparrow_renderer_page_texture");
            return nullptr;
        }

        page_texture->page    = page;
        page_texture->texture = tex;
        page_texture->fbo     = fbo;
        page_texture->rbo     = rbo;
        page_texture->width   = width;
        page_texture->height  = height;
    }

    wl_list_insert(&page->textures, &page_texture->link);

    return page_texture;
}

static bool create_backing_store(const FlutterBackingStoreConfig *config,
    FlutterBackingStore *backing_store_out,
    void *user_data)
{
#ifdef ENABLE_IMPELLER
    struct sparrow_renderer_page_texture *page_texture =
        page_get_texture(config->size.width, config->size.height, true);

    if (!page_texture)
    {
        return false;
    }

    backing_store_out->struct_size = sizeof(FlutterBackingStore);
    backing_store_out->user_data   = page_texture;
    backing_store_out->type = kFlutterBackingStoreTypeOpenGL;
    backing_store_out->did_update = false;

    backing_store_out->open_gl.type = kFlutterOpenGLTargetTypeFramebuffer;
    backing_store_out->open_gl.framebuffer.target = 0x8058; // GL_RGBA8
    backing_store_out->open_gl.framebuffer.name   = page_texture->fbo;
    backing_store_out->open_gl.framebuffer.user_data = page_texture;
    backing_store_out->open_gl.framebuffer.destruction_callback =
        texture_destruction_callback;
#else
    struct sparrow_renderer_page_texture *page_texture =
        page_get_texture(config->size.width, config->size.height, false);

    if (!page_texture)
    {
        return false;
    }

    backing_store_out->struct_size = sizeof(FlutterBackingStore);
    backing_store_out->user_data   = page_texture;
    backing_store_out->type = kFlutterBackingStoreTypeOpenGL;
    backing_store_out->did_update = false;

    backing_store_out->open_gl.type = kFlutterOpenGLTargetTypeTexture;
    backing_store_out->open_gl.texture.target = GL_TEXTURE_2D;
    backing_store_out->open_gl.texture.name   = page_texture->texture;
    backing_store_out->open_gl.texture.format = 0x93A1; // GL_BGRA8_EXT
    backing_store_out->open_gl.texture.user_data = page_texture;
    backing_store_out->open_gl.texture.destruction_callback =
        texture_destruction_callback;
    backing_store_out->open_gl.texture.width  = page_texture->width;
    backing_store_out->open_gl.texture.height = page_texture->height;
#endif

    return true;
}

static bool collect_backing_store(const FlutterBackingStore *backing_store,
    void *user_data)
{
    if (!backing_store)
    {
        return true;
    }

    sparrow_renderer_page_texture *page_texture =
        static_cast<sparrow_renderer_page_texture*>(backing_store->user_data);
    if (page_texture)
    {
        Core *instance = Core::instance();
        struct sparrow_renderer *renderer =
            instance ? &instance->sparrow_renderer : nullptr;

        if (instance && instance->egl_display)
        {
            EGLDisplay display   = instance->egl_display;
            EGLContext prev_ctx  = eglGetCurrentContext();
            EGLSurface prev_draw = eglGetCurrentSurface(EGL_DRAW);
            EGLSurface prev_read = eglGetCurrentSurface(EGL_READ);

            EGLContext resource_ctx =
                renderer ? renderer->main_thread_egl_context : EGL_NO_CONTEXT;
            if ((resource_ctx != EGL_NO_CONTEXT) && (prev_ctx != resource_ctx))
            {
                eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, resource_ctx);
            }

            if (page_texture->texture)
            {
                glDeleteTextures(1, &page_texture->texture);
                page_texture->texture = 0;
            }

            if (page_texture->rbo)
            {
                glDeleteRenderbuffers(1, &page_texture->rbo);
                page_texture->rbo = 0;
            }

            if (page_texture->fbo)
            {
                glDeleteFramebuffers(1, &page_texture->fbo);
                page_texture->fbo = 0;
            }

            if ((resource_ctx != EGL_NO_CONTEXT) && (prev_ctx != resource_ctx))
            {
                eglMakeCurrent(display, prev_draw, prev_read, prev_ctx);
            }
        }

        wl_list_remove(&page_texture->link);
        delete page_texture;
    }

    return true;
}

static bool present_layers(const FlutterLayer **f_layers, size_t layers_count,
    void *user_data)
{
    Core *instance = Core::instance();
    struct sparrow_renderer *renderer = &instance->sparrow_renderer;

    pthread_mutex_lock(&renderer->render_mutex);

    for (int i = 0; i < (int)renderer->current_scene.layers_count; i++)
    {
        struct sparrow_renderer_scene_layer *layer =
            &renderer->current_scene.layers[i];
        if ((layer->type == sceneLayerPlatform) && layer->platform.mutations)
        {
            free(layer->platform.mutations);
            layer->platform.mutations = nullptr;
        }
    }

    if (renderer->current_scene.layers)
    {
        free(renderer->current_scene.layers);
        renderer->current_scene.layers = nullptr;
        renderer->current_scene.layers_count = 0;
    }

    struct sparrow_renderer_scene_layer *layers =
        static_cast<sparrow_renderer_scene_layer*>(
            calloc(layers_count, sizeof(struct sparrow_renderer_scene_layer)));
    if (!layers)
    {
        pthread_mutex_unlock(&renderer->render_mutex);
        return false;
    }

    for (int i = 0; i < (int)layers_count; i++)
    {
        const FlutterLayer *f_layer = f_layers[i];
        struct sparrow_renderer_scene_layer *layer = &layers[i];

        layer->offset = f_layer->offset;
        layer->size   = f_layer->size;

        if (f_layer->type == kFlutterLayerContentTypeBackingStore)
        {
            layer->type = sceneLayerTexture;
            layer->texture.texture = static_cast<sparrow_renderer_page_texture*>(
                f_layer->backing_store->user_data);

            if ((f_layer->backing_store_present_info != nullptr) &&
                (f_layer->backing_store_present_info->paint_region != nullptr) &&
                (f_layer->backing_store_present_info->paint_region->rects_count >
                 0))
            {
                for (size_t r = 0;
                     r < f_layer->backing_store_present_info->paint_region->rects_count;
                     r++)
                {
                    const FlutterRect & rect =
                        f_layer->backing_store_present_info->paint_region->rects[r];
                    struct wlr_box d_box = {
                        .x     = (int)lround(layer->offset.x + rect.left),
                        .y     = (int)lround(layer->offset.y + rect.top),
                        .width = (int)lround(rect.right - rect.left),
                        .height = (int)lround(rect.bottom - rect.top),
                    };
                    if ((d_box.width > 0) && (d_box.height > 0))
                    {
                        bool is_fullscreen = (d_box.width >= (int)layer->size.width &&
                            d_box.height >= (int)layer->size.height);
                        sparrow_damage_add_box(&d_box, !is_fullscreen);
                    }
                }
            } else
            {
                struct wlr_box d_box = {
                    .x     = (int)lround(layer->offset.x),
                    .y     = (int)lround(layer->offset.y),
                    .width = (int)lround(layer->size.width),
                    .height = (int)lround(layer->size.height),
                };
                if ((d_box.width > 0) && (d_box.height > 0))
                {
                    sparrow_damage_add_box(&d_box, false);
                }
            }
        } else if (f_layer->type == kFlutterLayerContentTypePlatformView)
        {
            const FlutterPlatformView *platform_view = f_layer->platform_view;

            layer->type = sceneLayerPlatform;
            layer->platform.platform_view_id = platform_view->identifier;
            layer->platform.mutations_count  = platform_view->mutations_count;

            if (platform_view->mutations_count > 0)
            {
                FlutterPlatformViewMutation *mutations =
                    static_cast<FlutterPlatformViewMutation*>(
                        calloc(platform_view->mutations_count,
                            sizeof(FlutterPlatformViewMutation)));
                if (mutations)
                {
                    for (int k = 0; k < (int)platform_view->mutations_count; k++)
                    {
                        mutations[k] = *platform_view->mutations[k];
                    }
                }

                layer->platform.mutations = mutations;
            } else
            {
                layer->platform.mutations = nullptr;
            }
        }
    }

    renderer->current_scene.layers_count = layers_count;
    renderer->current_scene.layers = layers;
    renderer->current_scene.needs_update = true;

    if (renderer->current_scene.sync != 0)
    {
        eglDestroySync(instance->egl_display, renderer->current_scene.sync);
        renderer->current_scene.sync = 0;
    }

    renderer->current_scene.sync =
        eglCreateSync(instance->egl_display, EGL_SYNC_FENCE, nullptr);
    glFlush();
    renderer->current_page = (renderer->current_page == 0) ? 1 : 0;

    pthread_mutex_unlock(&renderer->render_mutex);
    return true;
}

void sparrow_renderer_init(gl_resolve_fn resolver)
{
    Core *instance = Core::instance();

    struct sparrow_renderer *renderer = &instance->sparrow_renderer;
    renderer->egl = instance->egl;

#ifndef USE_GLES32
    struct gl_fns *fns = &renderer->fns;

    fns->glGenFramebuffers =
        (void (*)(GLsizei, GLuint*))resolver("glGenFramebuffers");
    fns->glBindFramebuffer =
        (void (*)(GLenum, GLuint))resolver("glBindFramebuffer");
    fns->glGenTextures = (void (*)(GLsizei, GLuint*))resolver("glGenTextures");
    fns->glBindTexture = (void (*)(GLenum, GLuint))resolver("glBindTexture");
    fns->glTexImage2D  =
        (void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
            const void*))resolver("glTexImage2D");
    fns->glTexParameteri =
        (void (*)(GLenum, GLenum, GLint))resolver("glTexParameteri");
    fns->glFramebufferTexture =
        (void (*)(GLenum, GLenum, GLuint, GLint))resolver("glFramebufferTexture");
    fns->glDrawBuffers =
        (void (*)(GLsizei, const GLenum*))resolver("glDrawBuffers");
    fns->glCreateShader = (GLuint (*)(GLenum))(void*) resolver("glCreateShader");
    fns->glShaderSource = (void (*)(GLuint, GLsizei, const GLchar**,
        const GLint*))resolver("glShaderSource");
    fns->glCompileShader = (void (*)(GLuint))resolver("glCompileShader");
    fns->glGetShaderiv   =
        (void (*)(GLuint, GLenum, GLint*))resolver("glGetShaderiv");
    fns->glDeleteShader  = (void (*)(GLuint))resolver("glDeleteShader");
    fns->glCreateProgram = (GLuint (*)())(void*) resolver("glCreateProgram");
    fns->glAttachShader  = (void (*)(GLuint, GLuint))resolver("glAttachShader");
    fns->glLinkProgram   = (void (*)(GLuint))resolver("glLinkProgram");
    fns->glDetachShader  = (void (*)(GLuint, GLuint))resolver("glDetachShader");
    fns->glGetProgramiv  =
        (void (*)(GLuint, GLenum, GLint*))resolver("glGetProgramiv");
    fns->glDeleteProgram = (void (*)(GLuint))resolver("glDeleteProgram");
    fns->glGetUniformLocation = (GLint (*)(GLuint, const GLchar*))(
        void*) resolver("glGetUniformLocation");
    fns->glGetAttribLocation = (GLint (*)(GLuint, const GLchar*))(
        void*) resolver("glGetAttribLocation");
    fns->glActiveTexture = (void (*)(GLenum))resolver("glActiveTexture");
    fns->glUseProgram    = (void (*)(GLuint))resolver("glUseProgram");
    fns->glUniformMatrix3fv =
        (void (*)(GLint, GLsizei, GLboolean, const GLfloat*))resolver(
            "glUniformMatrix3fv");
    fns->glUniform1i = (void (*)(GLint, GLint))resolver("glUniform1i");
    fns->glUniform1f = (void (*)(GLint, GLfloat))resolver("glUniform1f");
    fns->glUniform2f = (void (*)(GLint, GLfloat, GLfloat))resolver("glUniform2f");
    fns->glUniform4f = (void (*)(GLint, GLfloat, GLfloat, GLfloat,
        GLfloat))resolver("glUniform4f");
    fns->glVertexAttribPointer =
        (void (*)(GLuint, GLint, GLenum, GLboolean, GLsizei,
            const void*))resolver("glVertexAttribPointer");
    fns->glEnableVertexAttribArray =
        (void (*)(GLuint))resolver("glEnableVertexAttribArray");
    fns->glDrawArrays =
        (void (*)(GLenum, GLint, GLsizei))resolver("glDrawArrays");
    fns->glDisableVertexAttribArray =
        (void (*)(GLuint))resolver("glDisableVertexAttribArray");
    fns->glEnable  = (void (*)(GLenum))resolver("glEnable");
    fns->glDisable = (void (*)(GLenum))resolver("glDisable");
    fns->glGetTextureImage = (void (*)(GLuint, GLint, GLenum, GLenum, GLsizei,
        void*))resolver("glGetTextureImage");
    fns->glCheckFramebufferStatus =
        (GLenum (*)(GLenum))(void*) resolver("glCheckFramebufferStatus");
    fns->glGetError = (GLenum (*)())(void*) resolver("glGetError");
    fns->glFramebufferTexture2D = (void (*)(
        GLenum, GLenum, GLenum, GLuint, GLint))resolver("glFramebufferTexture2D");
    fns->glGenBuffers = (void (*)(GLsizei, GLuint*))resolver("glGenBuffers");
    fns->glBindBuffer = (void (*)(GLenum, GLuint))resolver("glBindBuffer");
    fns->glBufferData = (void (*)(GLenum, GLsizeiptr, const void*,
        GLenum))resolver("glBufferData");
    fns->glClearColor =
        (void (*)(GLfloat, GLfloat, GLfloat, GLfloat))resolver("glClearColor");
    fns->glClear = (void (*)(GLbitfield))resolver("glClear");
    fns->glDeleteFramebuffers =
        (void (*)(GLsizei, GLuint*))resolver("glDeleteFramebuffers");
    fns->glDeleteTextures =
        (void (*)(GLsizei, GLuint*))resolver("glDeleteTextures");
    fns->glBindSampler = (void (*)(GLuint, GLuint))resolver("glBindSampler");
    fns->glBlendFuncSeparate =
        (void (*)(GLenum, GLenum, GLenum, GLenum))resolver("glBlendFuncSeparate");

    fns->glGetIntegerv = (void (*)(GLenum, GLint*))resolver("glGetIntegerv");
    fns->glGetBooleanv = (void (*)(GLenum, GLboolean*))resolver("glGetBooleanv");
    fns->glReadPixels  = (void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
        void*))resolver("glReadPixels");
    fns->glViewport =
        (void (*)(GLint, GLint, GLsizei, GLsizei))resolver("glViewport");
    fns->glIsEnabled = (GLboolean (*)(GLenum))(void*) resolver("glIsEnabled");
#endif

    const char *egl_exts = eglQueryString(instance->egl_display, EGL_EXTENSIONS);
    bool ext_context_priority =
        strstr(egl_exts, "EGL_IMG_context_priority") != nullptr;
    bool ext_context_robustness =
        strstr(egl_exts, "EGL_EXT_create_context_robustness") != nullptr;

#ifdef USE_DMABUF
    // Initialize DMA-BUF import support
    const bool ext_image_base    = strstr(egl_exts, "EGL_KHR_image_base") != nullptr;
    const bool ext_dmabuf_import =
        strstr(egl_exts, "EGL_EXT_image_dma_buf_import") != nullptr;

    renderer->has_dmabuf_import = ext_image_base && ext_dmabuf_import;

    if (renderer->has_dmabuf_import)
    {
        renderer->glEGLImageTargetTexture2DOES = (void (*)(
            GLenum, void*))eglGetProcAddress("glEGLImageTargetTexture2DOES");
        if (!renderer->glEGLImageTargetTexture2DOES)
        {
            renderer->has_dmabuf_import = false;
        }

    #ifndef USE_GLES32
        renderer->eglCreateImageKHR =
            (void*(*)(EGLDisplay, EGLContext, unsigned int, void*,
                const int*))eglGetProcAddress("eglCreateImageKHR");
        renderer->eglDestroyImageKHR =
            (int (*)(EGLDisplay, void*))eglGetProcAddress("eglDestroyImageKHR");

        if (!renderer->eglCreateImageKHR || !renderer->eglDestroyImageKHR ||
            !renderer->glEGLImageTargetTexture2DOES)
        {
            wlr_log(WLR_INFO,
                "DMA-BUF import extensions present but functions not found");
            renderer->has_dmabuf_import = false;
        } else
        {
            wlr_log(WLR_INFO, "DMA-BUF import enabled for zero-copy texture sharing");
        }

    #endif
    } else
    {
        wlr_log(WLR_INFO,
            "DMA-BUF import not available, using texture copy fallback");
    }

#endif

    EGLint client_version = 2;
    eglQueryContext(instance->egl_display, instance->egl_context,
        EGL_CONTEXT_CLIENT_VERSION, &client_version);

    EGLint context_priority   = EGL_CONTEXT_PRIORITY_MEDIUM_IMG;
    bool has_context_priority = false;
    if (ext_context_priority &&
        eglQueryContext(instance->egl_display, instance->egl_context,
            EGL_CONTEXT_PRIORITY_LEVEL_IMG, &context_priority))
    {
        has_context_priority = true;
    }

    EGLint reset_strategy   = EGL_LOSE_CONTEXT_ON_RESET_EXT;
    bool has_reset_strategy = false;
    if (ext_context_robustness &&
        eglQueryContext(instance->egl_display, instance->egl_context,
            EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT,
            &reset_strategy))
    {
        has_reset_strategy = true;
    }

    EGLint renderable_type = EGL_OPENGL_ES2_BIT;
#if defined (EGL_OPENGL_ES3_BIT)
    if (client_version >= 3)
    {
        renderable_type = EGL_OPENGL_ES3_BIT;
    }

#elif defined (EGL_OPENGL_ES3_BIT_KHR)
    if (client_version >= 3)
    {
        renderable_type = EGL_OPENGL_ES3_BIT_KHR;
    }

#endif

    size_t atti = 0;
    EGLint flutter_context_attribs[9];
    flutter_context_attribs[atti++] = EGL_CONTEXT_CLIENT_VERSION;
    flutter_context_attribs[atti++] = client_version;
    if (has_context_priority)
    {
        flutter_context_attribs[atti++] = EGL_CONTEXT_PRIORITY_LEVEL_IMG;
        flutter_context_attribs[atti++] = context_priority;
    }

    if (has_reset_strategy)
    {
        flutter_context_attribs[atti++] =
            EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT;
        flutter_context_attribs[atti++] = reset_strategy;
    }

    flutter_context_attribs[atti++] = EGL_NONE;

    EGLConfig flutter_config = EGL_NO_CONFIG_KHR;
    renderer->flutter_egl_context =
        eglCreateContext(instance->egl_display, EGL_NO_CONFIG_KHR,
            instance->egl_context, flutter_context_attribs);

    if (renderer->flutter_egl_context == EGL_NO_CONTEXT)
    {
        EGLint egl_error = eglGetError();
        wlr_log(WLR_INFO,
            "Configless context failed (0x%x), trying explicit config",
            egl_error);

        EGLConfig egl_config = EGL_NO_CONFIG_KHR;
        EGLint num_configs   = 0;
        EGLint egl_config_id = 0;

        if (eglQueryContext(instance->egl_display, instance->egl_context,
            EGL_CONFIG_ID, &egl_config_id))
        {
            const EGLint config_id_attribs[] = {EGL_CONFIG_ID, egl_config_id,
                EGL_NONE};

            if (eglChooseConfig(instance->egl_display, config_id_attribs, &egl_config,
                1, &num_configs) &&
                (num_configs > 0))
            {
                renderer->flutter_egl_context =
                    eglCreateContext(instance->egl_display, egl_config,
                        instance->egl_context, flutter_context_attribs);
                if (renderer->flutter_egl_context != EGL_NO_CONTEXT)
                {
                    flutter_config = egl_config;
                }
            }
        }

        if (renderer->flutter_egl_context == EGL_NO_CONTEXT)
        {
            const EGLint config_attribs[] = {EGL_SURFACE_TYPE,
                EGL_PBUFFER_BIT,
                EGL_RENDERABLE_TYPE,
                renderable_type,
                EGL_RED_SIZE,
                8,
                EGL_GREEN_SIZE,
                8,
                EGL_BLUE_SIZE,
                8,
                EGL_ALPHA_SIZE,
                8,
                EGL_NONE};

            if (eglChooseConfig(instance->egl_display, config_attribs, &egl_config, 1,
                &num_configs) &&
                (num_configs > 0))
            {
                renderer->flutter_egl_context =
                    eglCreateContext(instance->egl_display, egl_config,
                        instance->egl_context, flutter_context_attribs);
                if (renderer->flutter_egl_context != EGL_NO_CONTEXT)
                {
                    flutter_config = egl_config;
                }
            }
        }

        if (renderer->flutter_egl_context == EGL_NO_CONTEXT)
        {
            wlr_log(WLR_ERROR,
                "Could not create flutter EGL context! EGL error: 0x%x",
                eglGetError());
        }
    } else
    {
        flutter_config = EGL_NO_CONFIG_KHR;
    }

    if (renderer->flutter_egl_context != EGL_NO_CONTEXT)
    {
        renderer->flutter_resource_egl_context = eglCreateContext(
            instance->egl_display, flutter_config, renderer->flutter_egl_context,
            flutter_context_attribs);
        if (renderer->flutter_resource_egl_context == EGL_NO_CONTEXT)
        {
            wlr_log(WLR_ERROR,
                "Could not create flutter resource EGL context! EGL error: 0x%x",
                eglGetError());
        }

        renderer->main_thread_egl_context = eglCreateContext(
            instance->egl_display, flutter_config, renderer->flutter_egl_context,
            flutter_context_attribs);
        if (renderer->main_thread_egl_context == EGL_NO_CONTEXT)
        {
            wlr_log(WLR_ERROR,
                "Could not create main thread EGL context! EGL error: 0x%x",
                eglGetError());
        }
    }

    renderer->current_page = 0;
    renderer->current_scene.layers_count = 0;

    pthread_mutexattr_t render_mutex_attr;
    pthread_mutexattr_init(&render_mutex_attr);
    pthread_mutexattr_settype(&render_mutex_attr, PTHREAD_MUTEX_RECURSIVE);
    if (pthread_mutex_init(&renderer->render_mutex, &render_mutex_attr) != 0)
    {
        wlr_log(WLR_ERROR, "Could not init render mutex");
    }

    pthread_mutexattr_destroy(&render_mutex_attr);

    pthread_mutexattr_t texture_mutex_attr;
    pthread_mutexattr_init(&texture_mutex_attr);
    pthread_mutexattr_settype(&texture_mutex_attr, PTHREAD_MUTEX_RECURSIVE);
    if (pthread_mutex_init(&renderer->texture_mutex, &texture_mutex_attr) != 0)
    {
        wlr_log(WLR_ERROR, "Could not init texture mutex");
    }

    pthread_mutexattr_destroy(&texture_mutex_attr);

    eglMakeCurrent(instance->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
        instance->sparrow_renderer.flutter_egl_context);

    renderer->quad_rgbx_shader    = make_quad_rgbx_shader();
    renderer->quad_rounded_shader = make_quad_rounded_shader();
    // renderer->quad_external_shader = make_quad_external_shader();

#ifdef USE_GLES32
    glGenBuffers(1, &renderer->tex_coord_buffer);

    glBindBuffer(GL_ARRAY_BUFFER, renderer->tex_coord_buffer);

    glBufferData(GL_ARRAY_BUFFER, sizeof(texcoords), texcoords, GL_STATIC_DRAW);

    glGenBuffers(1, &renderer->quad_vert_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->quad_vert_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_verts), quad_verts, GL_STATIC_DRAW);
#else
    fns->glGenBuffers(1, &renderer->tex_coord_buffer);

    fns->glBindBuffer(GL_ARRAY_BUFFER, renderer->tex_coord_buffer);

    fns->glBufferData(GL_ARRAY_BUFFER, sizeof(texcoords), texcoords,
        GL_STATIC_DRAW);

    fns->glGenBuffers(1, &renderer->quad_vert_buffer);
    fns->glBindBuffer(GL_ARRAY_BUFFER, renderer->quad_vert_buffer);
    fns->glBufferData(GL_ARRAY_BUFFER, sizeof(quad_verts), quad_verts,
        GL_STATIC_DRAW);
#endif

    instance->fl_compositor.struct_size = sizeof(FlutterCompositor);
    instance->fl_compositor.user_data   = instance;
    instance->fl_compositor.avoid_backing_store_cache     = false;
    instance->fl_compositor.create_backing_store_callback = create_backing_store;
    instance->fl_compositor.collect_backing_store_callback =
        collect_backing_store;
    instance->fl_compositor.present_layers_callback = present_layers;

    for (int i = 0; i < SPARROW_RENDERER_NUM_PAGES; i++)
    {
        struct sparrow_renderer_page *page = &renderer->pages[i];
        wl_list_init(&page->textures);
        // wl_list_init(&page->unused_textures);
    }

    eglMakeCurrent(instance->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
        nullptr);
}

static FlutterTransformation flutter_transform_multiply(FlutterTransformation a, FlutterTransformation b)
{
    FlutterTransformation out = {
        .scaleX = a.scaleX * b.scaleX + a.skewX * b.skewY + a.transX * b.pers0,
        .skewX  = a.scaleX * b.skewX + a.skewX * b.scaleY + a.transX * b.pers1,
        .transX = a.scaleX * b.transX + a.skewX * b.transY + a.transX * b.pers2,
        .skewY  = a.skewY * b.scaleX + a.scaleY * b.skewY + a.transY * b.pers0,
        .scaleY = a.skewY * b.skewX + a.scaleY * b.scaleY + a.transY * b.pers1,
        .transY = a.skewY * b.transX + a.scaleY * b.transY + a.transY * b.pers2,
        .pers0  = a.pers0 * b.scaleX + a.pers1 * b.skewY + a.pers2 * b.pers0,
        .pers1  = a.pers0 * b.skewX + a.pers1 * b.scaleY + a.pers2 * b.pers1,
        .pers2  = a.pers0 * b.transX + a.pers1 * b.transY + a.pers2 * b.pers2,
    };
    return out;
}

static bool flutter_transform_is_affine(FlutterTransformation transform)
{
    return transform.pers0 == 0.0 && transform.pers1 == 0.0 &&
           transform.pers2 == 1.0;
}

static void flutter_transform_point(FlutterTransformation transform, double x,
    double y, double *out_x, double *out_y)
{
    *out_x = x * transform.scaleX + y * transform.skewX + transform.transX;
    *out_y = x * transform.skewY + y * transform.scaleY + transform.transY;
}

static int clamp_int(int v, int lo, int hi)
{
    if (v < lo)
    {
        return lo;
    }

    if (v > hi)
    {
        return hi;
    }

    return v;
}

static int ellipse_inset(double dy, int rx, int ry)
{
    if ((rx <= 0) || (ry <= 0))
    {
        return 0;
    }

    const double ady = fabs(dy);
    if (ady >= (double)ry)
    {
        return rx;
    }

    const double t = 1.0 - (ady * ady) / ((double)ry * (double)ry);
    if (t <= 0.0)
    {
        return rx;
    }

    const double x_boundary = (double)rx - (double)rx * sqrt(t);
    int inset = (int)ceil(x_boundary);
    if (inset < 0)
    {
        inset = 0;
    }

    if (inset > rx)
    {
        inset = rx;
    }

    return inset;
}

static struct wlr_box flutter_transform_rect(FlutterTransformation transform,
    double left, double top,
    double right, double bottom)
{
    // Transform a rectangle defined by float edges.
    // We floor/ceil the result to avoid "shrinking" as coordinates move
    // fractionally.
    double tx0, ty0, tx1, ty1, tx2, ty2, tx3, ty3;
    flutter_transform_point(transform, left, top, &tx0, &ty0);
    flutter_transform_point(transform, right, top, &tx1, &ty1);
    flutter_transform_point(transform, left, bottom, &tx2, &ty2);
    flutter_transform_point(transform, right, bottom, &tx3, &ty3);

    const double min_x = fmin(fmin(tx0, tx1), fmin(tx2, tx3));
    const double max_x = fmax(fmax(tx0, tx1), fmax(tx2, tx3));
    const double min_y = fmin(fmin(ty0, ty1), fmin(ty2, ty3));
    const double max_y = fmax(fmax(ty0, ty1), fmax(ty2, ty3));

    const double ix0 = floor(min_x);
    const double iy0 = floor(min_y);
    const double ix1 = ceil(max_x);
    const double iy1 = ceil(max_y);

    struct wlr_box transformed = {
        .x     = (int)ix0,
        .y     = (int)iy0,
        .width = (int)(ix1 - ix0),
        .height = (int)(iy1 - iy0),
    };

    return transformed;
}

static void pixman_region32_init_rounded_rect(pixman_region32_t *dst, int x,
    int y, int width, int height,
    int r_tl_x, int r_tl_y,
    int r_tr_x, int r_tr_y,
    int r_br_x, int r_br_y,
    int r_bl_x, int r_bl_y)
{
    pixman_region32_init(dst);

    if ((width <= 0) || (height <= 0))
    {
        return;
    }

    // Clamp radii to sane values.
    const int half_w = width / 2;
    const int half_h = height / 2;
    r_tl_x = clamp_int(r_tl_x, 0, half_w);
    r_tr_x = clamp_int(r_tr_x, 0, half_w);
    r_br_x = clamp_int(r_br_x, 0, half_w);
    r_bl_x = clamp_int(r_bl_x, 0, half_w);
    r_tl_y = clamp_int(r_tl_y, 0, half_h);
    r_tr_y = clamp_int(r_tr_y, 0, half_h);
    r_br_y = clamp_int(r_br_y, 0, half_h);
    r_bl_y = clamp_int(r_bl_y, 0, half_h);

    const int top_band    = r_tl_y > r_tr_y ? r_tl_y : r_tr_y;
    const int bottom_band = r_bl_y > r_br_y ? r_bl_y : r_br_y;

    // Middle band: full width.
    const int middle_h = height - top_band - bottom_band;
    if (middle_h > 0)
    {
        pixman_region32_union_rect(dst, dst, x, y + top_band, width, middle_h);
    }

    // Top band rows.
    for (int row = 0; row < top_band; row++)
    {
        const double y_center = (double)row + 0.5;
        int inset_left  = 0;
        int inset_right = 0;

        if ((r_tl_y > 0) && (row < r_tl_y))
        {
            const double dy = y_center - (double)r_tl_y;
            inset_left = ellipse_inset(dy, r_tl_x, r_tl_y);
        }

        if ((r_tr_y > 0) && (row < r_tr_y))
        {
            const double dy = y_center - (double)r_tr_y;
            inset_right = ellipse_inset(dy, r_tr_x, r_tr_y);
        }

        const int w = width - inset_left - inset_right;
        if (w > 0)
        {
            pixman_region32_union_rect(dst, dst, x + inset_left, y + row, w, 1);
        }
    }

    // Bottom band rows.
    for (int row = height - bottom_band; row < height; row++)
    {
        if (row < 0)
        {
            continue;
        }

        const double y_center = (double)row + 0.5;
        int inset_left  = 0;
        int inset_right = 0;

        if ((r_bl_y > 0) && (row >= height - r_bl_y))
        {
            const double dy = y_center - (double)(height - r_bl_y);
            inset_left = ellipse_inset(dy, r_bl_x, r_bl_y);
        }

        if ((r_br_y > 0) && (row >= height - r_br_y))
        {
            const double dy = y_center - (double)(height - r_br_y);
            inset_right = ellipse_inset(dy, r_br_x, r_br_y);
        }

        const int w = width - inset_left - inset_right;
        if (w > 0)
        {
            pixman_region32_union_rect(dst, dst, x + inset_left, y + row, w, 1);
        }
    }
}

static struct wlr_box scale_box(struct wlr_box box, double scale)
{
    if (scale == 1.0)
    {
        return box;
    }

    struct wlr_box scaled = {
        .x     = (int)lround((double)box.x * scale),
        .y     = (int)lround((double)box.y * scale),
        .width = (int)lround((double)box.width * scale),
        .height = (int)lround((double)box.height * scale),
    };

    return scaled;
}

struct sparrow_surface_render_data
{
    struct wlr_render_pass *render_pass = nullptr;
    FlutterTransformation transform;
    double output_scale = 0;
    const pixman_region32_t *clip = nullptr;
    const float *alpha     = nullptr;
    double content_scale_x = 0;
    double content_scale_y = 0;
    int viewport_x     = 0; // Output viewport offset for multi-monitor
    int viewport_y     = 0;
    int viewport_width = 0;
    int viewport_height = 0;
    enum wl_output_transform output_transform = WL_OUTPUT_TRANSFORM_NORMAL;
};

struct sparrow_rounded_clip
{
    bool active;
    float rect_x;
    float rect_y;
    float rect_w;
    float rect_h;
    float radius_tl;
    float radius_tr;
    float radius_br;
    float radius_bl;
};

struct sparrow_rounded_render_data
{
    FlutterTransformation transform;
    double output_scale = 0;
    float opacity = 0;
    double content_scale_x = 0;
    double content_scale_y = 0;
    struct sparrow_rounded_clip rounded_clip = {};
    int viewport_x     = 0; // Output viewport offset for multi-monitor
    int viewport_y     = 0;
    int viewport_width = 0;
    int viewport_height = 0;
    int buffer_width    = 0;
    int buffer_height   = 0;
    enum wl_output_transform output_transform = WL_OUTPUT_TRANSFORM_NORMAL;
};

static void render_surface_rounded_iterator(struct wlr_surface *surface, int sx,
    int sy, void *data)
{
    struct sparrow_rounded_render_data *render_data =
        static_cast<sparrow_rounded_render_data*>(data);
    Core *instance = Core::instance();
    struct sparrow_renderer *renderer = &instance->sparrow_renderer;
#ifndef USE_GLES32
    const struct gl_fns *fns = &renderer->fns;
#endif
    struct wlr_surface_state *state = &surface->current;
    struct wlr_texture *texture     = sparrow_surface_get_texture(surface);
    if (texture == nullptr)
    {
        wlr_log(WLR_DEBUG,
            "render_surface_rounded_iterator: no texture for surface %p",
            (void*)surface);
        return;
    }

    float src_x = 0.0f;
    float src_y = 0.0f;
    float src_w = (float)state->width;
    float src_h = (float)state->height;

    // bool has_viewport = false;
    if (state->viewport.has_src)
    {
        // has_viewport = true;
        src_x = state->viewport.src.x;
        src_y = state->viewport.src.y;
        src_w = state->viewport.src.width;
        src_h = state->viewport.src.height;
        wlr_log(
            WLR_INFO,
            "VIEWPORT SRC detected: x=%.2f y=%.2f w=%.2f h=%.2f (surface %dx%d)",
            src_x, src_y, src_w, src_h, state->width, state->height);
    }

    if (state->viewport.has_dst)
    {
        wlr_log(WLR_INFO, "VIEWPORT DST detected: dst_w=%d dst_h=%d",
            state->viewport.dst_width, state->viewport.dst_height);
    }

    int dst_w = state->width;
    int dst_h = state->height;
    if (state->viewport.has_dst)
    {
        dst_w = state->viewport.dst_width;
        dst_h = state->viewport.dst_height;
    }

    const double left   = (double)sx * render_data->content_scale_x;
    const double top    = (double)sy * render_data->content_scale_y;
    const double right  = left + (double)dst_w * render_data->content_scale_x;
    const double bottom = top + (double)dst_h * render_data->content_scale_y;

    struct wlr_box dst_box =
        flutter_transform_rect(render_data->transform, left, top, right, bottom);
    dst_box = scale_box(dst_box, render_data->output_scale);

    dst_box.x -= render_data->viewport_x;
    dst_box.y -= render_data->viewport_y;

    if (render_data->output_transform != WL_OUTPUT_TRANSFORM_NORMAL)
    {
        wlr_box_transform(&dst_box, &dst_box, render_data->output_transform,
            render_data->viewport_width,
            render_data->viewport_height);
    }

    const GLfloat _texcoords[8] = {
        src_x / texture->width, src_y / texture->height,
        (src_x + src_w) / texture->width, src_y / texture->height,
        (src_x + src_w) / texture->width, (src_y + src_h) / texture->height,
        src_x / texture->width, (src_y + src_h) / texture->height,
    };

    wlr_log(WLR_DEBUG,
        "Rendering surface %p | src_box(%.1f,%.1f,%.1f,%.1f) | "
        "dst_box(%d,%d,%d,%d) | scale=%.2f",
        (void*)surface, src_x, src_y, src_w, src_h, dst_box.x, dst_box.y,
        dst_box.width, dst_box.height, render_data->output_scale);

    const int view_width =
        render_data->buffer_width > 0 ?
        render_data->buffer_width :
        (render_data->viewport_width > 0 ? render_data->viewport_width : 1);
    const int view_height =
        render_data->buffer_height > 0 ?
        render_data->buffer_height :
        (render_data->viewport_height > 0 ? render_data->viewport_height :
            1);

    const float ndc_left  = ((float)dst_box.x / (float)view_width) * 2.0f - 1.0f;
    const float ndc_right =
        ((float)(dst_box.x + dst_box.width) / (float)view_width) * 2.0f - 1.0f;
    const float ndc_top    = 1.0f - ((float)dst_box.y / (float)view_height) * 2.0f;
    const float ndc_bottom =
        1.0f - ((float)(dst_box.y + dst_box.height) / (float)view_height) * 2.0f;

    const GLfloat verts[8] = {
        ndc_right, ndc_bottom, ndc_left, ndc_bottom,
        ndc_right, ndc_top, ndc_left, ndc_top,
    };

    struct quad_rounded_shader *shader = &renderer->quad_rounded_shader;

#ifdef USE_GLES32
    glUseProgram(shader->prog);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
        GL_ONE_MINUS_SRC_ALPHA);

    glActiveTexture(GL_TEXTURE0);
    struct wlr_gles2_texture_attribs tex_attribs;
    wlr_gles2_texture_get_attribs(texture, &tex_attribs);
    glBindTexture(tex_attribs.target, tex_attribs.tex);

    glUniform1i(shader->tex, 0);
    glUniform1f(shader->alpha, render_data->opacity);

    struct sparrow_rounded_clip *rc = &render_data->rounded_clip;
    glUniform4f(shader->clip_rect, rc->rect_x, rc->rect_y, rc->rect_w,
        rc->rect_h);
    glUniform4f(shader->corner_radii, rc->radius_tl, rc->radius_tr, rc->radius_br,
        rc->radius_bl);
    glUniform1f(shader->output_height, (float)view_height);

    glEnableVertexAttribArray(shader->pos_attrib);
    glVertexAttribPointer(shader->pos_attrib, 2, GL_FLOAT, GL_FALSE, 0, verts);

    glEnableVertexAttribArray(shader->tex_attrib);
    glVertexAttribPointer(shader->tex_attrib, 2, GL_FLOAT, GL_FALSE, 0,
        _texcoords);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(shader->pos_attrib);
    glDisableVertexAttribArray(shader->tex_attrib);

    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glDisable(GL_BLEND);
#else
    fns->glUseProgram(shader->prog);
    fns->glEnable(GL_BLEND);
    fns->glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
        GL_ONE_MINUS_SRC_ALPHA);

    fns->glActiveTexture(GL_TEXTURE0);
    struct wlr_gles2_texture_attribs tex_attribs;
    wlr_gles2_texture_get_attribs(texture, &tex_attribs);
    fns->glBindTexture(tex_attribs.target, tex_attribs.tex);

    fns->glUniform1i(shader->tex, 0);
    fns->glUniform1f(shader->alpha, render_data->opacity);

    struct sparrow_rounded_clip *rc = &render_data->rounded_clip;
    fns->glUniform4f(shader->clip_rect, rc->rect_x, rc->rect_y, rc->rect_w,
        rc->rect_h);
    fns->glUniform4f(shader->corner_radii, rc->radius_tl, rc->radius_tr,
        rc->radius_br, rc->radius_bl);
    fns->glUniform1f(shader->output_height, (float)view_height);

    fns->glEnableVertexAttribArray(shader->pos_attrib);
    fns->glVertexAttribPointer(shader->pos_attrib, 2, GL_FLOAT, GL_FALSE, 0,
        verts);

    fns->glEnableVertexAttribArray(shader->tex_attrib);
    fns->glVertexAttribPointer(shader->tex_attrib, 2, GL_FLOAT, GL_FALSE, 0,
        _texcoords);

    fns->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    fns->glDisableVertexAttribArray(shader->pos_attrib);
    fns->glDisableVertexAttribArray(shader->tex_attrib);

    fns->glBindTexture(GL_TEXTURE_2D, 0);
    fns->glUseProgram(0);
    fns->glDisable(GL_BLEND);
#endif
}

static void render_surface_iterator(struct wlr_surface *surface, int sx, int sy,
    void *data)
{
    struct sparrow_surface_render_data *render_data =
        static_cast<sparrow_surface_render_data*>(data);
    struct wlr_surface_state *state = &surface->current;
    struct wlr_texture *texture     = sparrow_surface_get_texture(surface);
    if (texture == nullptr)
    {
        wlr_log(WLR_DEBUG, "render_surface_iterator: no texture for surface %p",
            (void*)surface);
        return;
    }

    float src_x = 0.0f;
    float src_y = 0.0f;
    float src_w = (float)state->width;
    float src_h = (float)state->height;

    const bool has_src = state->viewport.has_src;
    const bool has_dst = state->viewport.has_dst;

    if (has_src)
    {
        src_x = state->viewport.src.x;
        src_y = state->viewport.src.y;
        src_w = state->viewport.src.width;
        src_h = state->viewport.src.height;

        wlr_log(WLR_INFO,
            "[VIEWPORT] SRC detected on surface %p : x=%.2f y=%.2f w=%.2f "
            "h=%.2f (surface size %dx%d)",
            (void*)surface, src_x, src_y, src_w, src_h, state->width,
            state->height);
    }

    if (has_dst)
    {
        wlr_log(
            WLR_INFO, "[VIEWPORT] DST detected on surface %p : dst_w=%d dst_h=%d",
            (void*)surface, state->viewport.dst_width, state->viewport.dst_height);
    }

    int dst_w = state->width;
    int dst_h = state->height;
    if (has_dst)
    {
        dst_w = state->viewport.dst_width;
        dst_h = state->viewport.dst_height;
    }

    const double left   = (double)sx * render_data->content_scale_x;
    const double top    = (double)sy * render_data->content_scale_y;
    const double right  = left + (double)dst_w * render_data->content_scale_x;
    const double bottom = top + (double)dst_h * render_data->content_scale_y;

    struct wlr_box dst_box =
        flutter_transform_rect(render_data->transform, left, top, right, bottom);
    dst_box = scale_box(dst_box, render_data->output_scale);

    dst_box.x -= render_data->viewport_x;
    dst_box.y -= render_data->viewport_y;

    if (render_data->output_transform != WL_OUTPUT_TRANSFORM_NORMAL)
    {
        wlr_box_transform(&dst_box, &dst_box, render_data->output_transform,
            render_data->viewport_width,
            render_data->viewport_height);
    }

    const enum wl_output_transform surface_transform =
        wlr_output_transform_invert(state->transform);
    const enum wl_output_transform final_transform = wlr_output_transform_compose(
        surface_transform, render_data->output_transform);

    float alpha = 1.0f;
    if (render_data->alpha)
    {
        alpha = *render_data->alpha;
    }

    wlr_log(WLR_DEBUG,
        "[RENDER] surface %p | src(%.1f,%.1f,%.1f,%.1f) | dst(%d,%d,%d,%d) | "
        "alpha=%.2f",
        (void*)surface, src_x, src_y, src_w, src_h, dst_box.x, dst_box.y,
        dst_box.width, dst_box.height, alpha);

    struct wlr_render_texture_options opts = {
        .texture = texture,
        .src_box = {.x = src_x, .y = src_y, .width = src_w, .height = src_h},
        .dst_box = dst_box,
        .alpha   = render_data->alpha, // float, pas pointeur
        .clip    = render_data->clip,
        .transform   = final_transform,
        .filter_mode = WLR_SCALE_FILTER_BILINEAR,
    };

    wlr_render_pass_add_texture(render_data->render_pass, &opts);
}

struct sparrow_presentation_data
{
    struct wlr_output *wlr_output;
    struct timespec *now;
};

static void send_presentation_and_frame_done_iterator(struct wlr_surface *surface, int sx,
    int sy, void *data)
{
    struct sparrow_presentation_data *pdata =
        static_cast<struct sparrow_presentation_data*>(data);
    if (pdata->wlr_output != nullptr)
    {
        wlr_presentation_surface_textured_on_output(surface, pdata->wlr_output);
    }
}

static void render_scene_layer_platform(struct wlr_render_pass *render_pass,
    struct sparrow_renderer_scene_layer *layer,
    struct timespec *now,
    const struct sparrow_output_viewport *viewport,
    pixman_region32_t *damage_region = nullptr)
{
    SPARROW_GL_SCOPE("Sparrow::PlatformViewLayer");
    Core *instance = Core::instance();

    const uint32_t view_handle = layer->platform.platform_view_id;
    // struct sparrow_renderer *renderer = &instance->sparrow_renderer;

    SparrowView *view = nullptr;
    SparrowView *v    = nullptr;
    wl_list_for_each(v, &instance->views_list, link)
    {
        if (v && (v->handle == view_handle))
        {
            view = v;
            break;
        }
    }
    if (!view)
    {
        wlr_log(WLR_ERROR, "Got invalid view handle! (%d)", view_handle);
        return;
    }

    // pthread_mutex_lock(&renderer->render_mutex);

    // Use per-view output scale for multi-monitor support
    // The view's current_output is updated when its position changes
    double output_scale = 1.0;
    if ((view->current_output != nullptr) &&
        (view->current_output->wlr_output != nullptr))
    {
        output_scale = view->current_output->wlr_output->scale;
    } else
    {
        // Fallback to first output for backwards compatibility
        Output *first_output = sparrow_get_first_output();
        if ((first_output != nullptr) && (first_output->wlr_output != nullptr))
        {
            output_scale = first_output->wlr_output->scale;
        }
    }

    // Store viewport info for coordinate transformation
    const int viewport_x     = viewport->x;
    const int viewport_y     = viewport->y;
    const int viewport_width = viewport->width;
    const int viewport_height = viewport->height;

    // During fast interactive resize, Flutter can resize the PlatformView widget
    // before the client commits a new buffer, revealing the frame background on
    // the opposite edge. To keep things visually solid, temporarily scale the
    // last committed client buffer up to the widget size whenever the widget is
    // larger than the client buffer. When the client catches up, the scale
    // returns to 1.0 automatically.
    //
    // NOTE: This scaling should ONLY apply when there's a genuine size mismatch
    // (e.g., during resize). Persistent scaling causes blur and rendering issues.
    double content_scale_x = 1.0;
    double content_scale_y = 1.0;
    const int surf_w = view->xdg_surface->surface->current.width;
    const int surf_h = view->xdg_surface->surface->current.height;
    if ((surf_w > 0) && (surf_h > 0) && (layer->size.width > 0.0) &&
        (layer->size.height > 0.0))
    {
        const double sx = layer->size.width / (double)surf_w;
        const double sy = layer->size.height / (double)surf_h;

        // Only scale up (this is what prevents "empty strip" on expansion).
        // Threshold increased to 1.01 (1% tolerance) to avoid micro-scaling
        // due to floating point rounding in the Flutter->C path.
        if (isfinite(sx) && (sx > 1.01))
        {
            content_scale_x = sx;
        }

        if (isfinite(sy) && (sy > 1.01))
        {
            content_scale_y = sy;
        }

        if (!isfinite(content_scale_x) || (content_scale_x <= 0.0))
        {
            content_scale_x = 1.0;
        }

        if (!isfinite(content_scale_y) || (content_scale_y <= 0.0))
        {
            content_scale_y = 1.0;
        }
    }

    float opacity = 1.0f;
    FlutterTransformation current_transform = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    bool transform_affine = true;
    bool has_clip = false;
    bool has_rounded_clip = false;
    struct sparrow_rounded_clip rounded_clip = {};
    pixman_region32_t clip_region;

    for (size_t m = 0; m < layer->platform.mutations_count; m++)
    {
        const FlutterPlatformViewMutation *mutation = &layer->platform.mutations[m];
        switch (mutation->type)
        {
          case kFlutterPlatformViewMutationTypeOpacity:
        {
            opacity *= mutation->opacity;
            break;
        }

          case kFlutterPlatformViewMutationTypeTransformation:
        {
            const FlutterTransformation mutation_transform = mutation->transformation;
            if (!flutter_transform_is_affine(mutation_transform))
            {
                transform_affine = false;
                break;
            }

            current_transform =
                flutter_transform_multiply(mutation_transform, current_transform);
            break;
        }

          case kFlutterPlatformViewMutationTypeClipRect:
        {
            // IMPORTANT: Don't truncate clip edges before transforming.
            // Truncation causes the effective clip to "shrink" by ~1px as the window
            // moves through fractional coordinates (visible as content getting cut
            // off towards bottom-right).
            struct wlr_box mutation_box = flutter_transform_rect(
                current_transform, mutation->clip_rect.left, mutation->clip_rect.top,
                mutation->clip_rect.right, mutation->clip_rect.bottom);
            mutation_box = scale_box(mutation_box, output_scale);

            pixman_region32_t tmp;
            pixman_region32_init_rect(&tmp, mutation_box.x, mutation_box.y,
                mutation_box.width, mutation_box.height);
            if (!has_clip)
            {
                pixman_region32_init(&clip_region);
                pixman_region32_copy(&clip_region, &tmp);
                has_clip = true;
            } else
            {
                pixman_region32_intersect(&clip_region, &clip_region, &tmp);
            }

            pixman_region32_fini(&tmp);
            break;
        }

          case kFlutterPlatformViewMutationTypeClipRoundedRect:
        {
            struct wlr_box mutation_box = flutter_transform_rect(
                current_transform, mutation->clip_rounded_rect.rect.left,
                mutation->clip_rounded_rect.rect.top,
                mutation->clip_rounded_rect.rect.right,
                mutation->clip_rounded_rect.rect.bottom);
            mutation_box = scale_box(mutation_box, output_scale);

            const bool disable_radius = view->maximized || view->fullscreen;

            const float r_tl =
                (float)(mutation->clip_rounded_rect.upper_left_corner_radius.width *
                    output_scale);
            const float r_tr =
                (float)(mutation->clip_rounded_rect.upper_right_corner_radius.width *
                    output_scale);
            const float r_br =
                (float)(mutation->clip_rounded_rect.lower_right_corner_radius.width *
                    output_scale);
            const float r_bl =
                (float)(mutation->clip_rounded_rect.lower_left_corner_radius.width *
                    output_scale);

            const bool has_any_radius =
                !disable_radius &&
                (r_tl > 0.5f || r_tr > 0.5f || r_br > 0.5f || r_bl > 0.5f);

            if (has_any_radius && !has_rounded_clip)
            {
                has_rounded_clip    = true;
                rounded_clip.active = true;
                rounded_clip.rect_x = (float)mutation_box.x;
                rounded_clip.rect_y = (float)mutation_box.y;
                rounded_clip.rect_w = (float)mutation_box.width;
                rounded_clip.rect_h = (float)mutation_box.height;
                rounded_clip.radius_tl = r_tl;
                rounded_clip.radius_tr = r_tr;
                rounded_clip.radius_br = r_br;
                rounded_clip.radius_bl = r_bl;
            }

            pixman_region32_t tmp;
            if (disable_radius || !has_any_radius)
            {
                pixman_region32_init_rect(&tmp, mutation_box.x, mutation_box.y,
                    mutation_box.width, mutation_box.height);
            } else
            {
                const int r_tl_x = (int)lround(
                    mutation->clip_rounded_rect.upper_left_corner_radius.width *
                    output_scale);
                const int r_tl_y = (int)lround(
                    mutation->clip_rounded_rect.upper_left_corner_radius.height *
                    output_scale);
                const int r_tr_x = (int)lround(
                    mutation->clip_rounded_rect.upper_right_corner_radius.width *
                    output_scale);
                const int r_tr_y = (int)lround(
                    mutation->clip_rounded_rect.upper_right_corner_radius.height *
                    output_scale);
                const int r_br_x = (int)lround(
                    mutation->clip_rounded_rect.lower_right_corner_radius.width *
                    output_scale);
                const int r_br_y = (int)lround(
                    mutation->clip_rounded_rect.lower_right_corner_radius.height *
                    output_scale);
                const int r_bl_x = (int)lround(
                    mutation->clip_rounded_rect.lower_left_corner_radius.width *
                    output_scale);
                const int r_bl_y = (int)lround(
                    mutation->clip_rounded_rect.lower_left_corner_radius.height *
                    output_scale);

                pixman_region32_init_rounded_rect(
                    &tmp, mutation_box.x, mutation_box.y, mutation_box.width,
                    mutation_box.height, r_tl_x, r_tl_y, r_tr_x, r_tr_y, r_br_x, r_br_y,
                    r_bl_x, r_bl_y);
            }

            if (!has_clip)
            {
                pixman_region32_init(&clip_region);
                pixman_region32_copy(&clip_region, &tmp);
                has_clip = true;
            } else
            {
                pixman_region32_intersect(&clip_region, &clip_region, &tmp);
            }

            pixman_region32_fini(&tmp);
            break;
        }

          default:
            break;
        }
    }

#ifdef DAMAGE_HISTORY
    // Intersect with frame damage region if provided
    if ((damage_region != nullptr) && pixman_region32_not_empty(damage_region))
    {
        if (!has_clip)
        {
            pixman_region32_init(&clip_region);
            pixman_region32_copy(&clip_region, damage_region);
            has_clip = true;
        } else
        {
            pixman_region32_intersect(&clip_region, &clip_region, damage_region);
        }
    }

#endif

    if (!transform_affine)
    {
        wlr_log(WLR_INFO,
            "Non-affine Flutter transform not supported for platform view %d",
            view_handle);
    }

    if (has_clip)
    {
        const pixman_box32_t *ext = pixman_region32_extents(&clip_region);
        wlr_log(
            WLR_DEBUG,
            "platform view %d offset(%.2f,%.2f) size(%.2f,%.2f) output_scale %.2f "
            "transform[%.3f %.3f %.3f %.3f %.3f %.3f] clip_extents(%d,%d,%d,%d)",
            view_handle, (double)layer->offset.x, (double)layer->offset.y,
            (double)layer->size.width, (double)layer->size.height,
            (double)output_scale, (double)current_transform.scaleX,
            (double)current_transform.skewX, (double)current_transform.transX,
            (double)current_transform.skewY, (double)current_transform.scaleY,
            (double)current_transform.transY, ext->x1, ext->y1,
            (int)(ext->x2 - ext->x1), (int)(ext->y2 - ext->y1));
    } else
    {
        wlr_log(WLR_DEBUG,
            "platform view %d offset(%.2f,%.2f) size(%.2f,%.2f) output_scale "
            "%.2f transform[%.3f %.3f %.3f %.3f %.3f %.3f] no_clip",
            view_handle, (double)layer->offset.x, (double)layer->offset.y,
            (double)layer->size.width, (double)layer->size.height,
            (double)output_scale, (double)current_transform.scaleX,
            (double)current_transform.skewX, (double)current_transform.transX,
            (double)current_transform.skewY, (double)current_transform.scaleY,
            (double)current_transform.transY);
    }

    if (has_rounded_clip && rounded_clip.active)
    {
        struct sparrow_rounded_render_data rounded_render_data = {
            .transform    = current_transform,
            .output_scale = output_scale,
            .opacity = opacity,
            .content_scale_x = content_scale_x,
            .content_scale_y = content_scale_y,
            .rounded_clip    = rounded_clip,
            .viewport_x     = viewport_x,
            .viewport_y     = viewport_y,
            .viewport_width = viewport_width,
            .viewport_height  = viewport_height,
            .buffer_width     = viewport->buffer_width,
            .buffer_height    = viewport->buffer_height,
            .output_transform = viewport->transform,
        };

        wlr_surface_for_each_surface(view->xdg_surface->surface,
            render_surface_rounded_iterator,
            &rounded_render_data);
    } else
    {
        struct sparrow_surface_render_data render_data = {
            .render_pass  = render_pass,
            .transform    = current_transform,
            .output_scale = output_scale,
            .clip  = has_clip ? &clip_region : nullptr,
            .alpha = &opacity,
            .content_scale_x = content_scale_x,
            .content_scale_y = content_scale_y,
            .viewport_x     = viewport_x,
            .viewport_y     = viewport_y,
            .viewport_width = viewport_width,
            .viewport_height  = viewport_height,
            .output_transform = viewport->transform,
        };

        wlr_surface_for_each_surface(view->xdg_surface->surface,
            render_surface_iterator, &render_data);
    }

    if (has_clip)
    {
        pixman_region32_fini(&clip_region);
    }

    // Report presentation to the output where this surface is displayed
    Output *pres_output = view->current_output;
    if (pres_output == nullptr)
    {
        pres_output = sparrow_get_first_output(); // Fallback
    }

    struct sparrow_presentation_data pdata = {
        .wlr_output = pres_output != nullptr ? pres_output->wlr_output : nullptr,
        .now = now,
    };
    wlr_surface_for_each_surface(view->xdg_surface->surface,
        send_presentation_and_frame_done_iterator,
        &pdata);
}

#ifdef USE_DMABUF

    #ifndef GL_TEXTURE_EXTERNAL_OES
        #define GL_TEXTURE_EXTERNAL_OES 0x8D65
    #endif

// Helper to find or create an EGLImage for a buffer in the cache
static void *get_or_create_egl_image(struct wlr_dmabuf_attributes *dmabuf_attribs)
{
    // struct sparrow_renderer *renderer = &instance->sparrow_renderer;
    Core *instance = Core::instance();

    // Validate plane count (defensive check - wlroots guarantees max 4 planes)
    if ((dmabuf_attribs->n_planes <= 0) ||
        (dmabuf_attribs->n_planes > WLR_DMABUF_MAX_PLANES))
    {
        wlr_log(WLR_ERROR, "Invalid DMA-BUF plane count: %d (max: %d)",
            dmabuf_attribs->n_planes, WLR_DMABUF_MAX_PLANES);
        return nullptr;
    }

    // Build EGLImage attributes for DMA-BUF import
    unsigned int atti = 0;
    EGLint attribs[50];
    attribs[atti++] = EGL_WIDTH;
    attribs[atti++] = dmabuf_attribs->width;
    attribs[atti++] = EGL_HEIGHT;
    attribs[atti++] = dmabuf_attribs->height;
    attribs[atti++] = EGL_LINUX_DRM_FOURCC_EXT;
    attribs[atti++] = dmabuf_attribs->format;

    // Define attribute names for each plane
    struct
    {
        EGLint fd;
        EGLint offset;
        EGLint pitch;
        EGLint mod_lo;
        EGLint mod_hi;
    } plane_attrs[WLR_DMABUF_MAX_PLANES] = {
        {EGL_DMA_BUF_PLANE0_FD_EXT, EGL_DMA_BUF_PLANE0_OFFSET_EXT,
            EGL_DMA_BUF_PLANE0_PITCH_EXT, EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
            EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT},
        {EGL_DMA_BUF_PLANE1_FD_EXT, EGL_DMA_BUF_PLANE1_OFFSET_EXT,
            EGL_DMA_BUF_PLANE1_PITCH_EXT, EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT,
            EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT},
        {EGL_DMA_BUF_PLANE2_FD_EXT, EGL_DMA_BUF_PLANE2_OFFSET_EXT,
            EGL_DMA_BUF_PLANE2_PITCH_EXT, EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT,
            EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT},
        {EGL_DMA_BUF_PLANE3_FD_EXT, EGL_DMA_BUF_PLANE3_OFFSET_EXT,
            EGL_DMA_BUF_PLANE3_PITCH_EXT, EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT,
            EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT},
    };

    for (int i = 0; i < dmabuf_attribs->n_planes; i++)
    {
        attribs[atti++] = plane_attrs[i].fd;
        attribs[atti++] = dmabuf_attribs->fd[i];
        attribs[atti++] = plane_attrs[i].offset;
        attribs[atti++] = (EGLint)dmabuf_attribs->offset[i];
        attribs[atti++] = plane_attrs[i].pitch;
        attribs[atti++] = (EGLint)dmabuf_attribs->stride[i];

        // Add modifier if not INVALID
        if (dmabuf_attribs->modifier != DRM_FORMAT_MOD_INVALID)
        {
            attribs[atti++] = plane_attrs[i].mod_lo;
            attribs[atti++] = dmabuf_attribs->modifier & 0xFFFFFFFF;
            attribs[atti++] = plane_attrs[i].mod_hi;
            attribs[atti++] = dmabuf_attribs->modifier >> 32;
        }
    }

    attribs[atti++] = EGL_NONE;

    // Create EGLImage from DMA-BUF
    void *egl_image = instance->egl->procs.eglCreateImageKHR(
        instance->egl_display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr,
        attribs);

    if (egl_image == EGL_NO_IMAGE_KHR)
    {
        return nullptr;
    }

    return egl_image;
}

bool wlr_egl_restore_context(struct wlr_egl_context *context)
{
    // If the saved context is a null-context, we must use the current
    // display instead of the saved display because eglMakeCurrent() can't
    // handle EGL_NO_DISPLAY.
    EGLDisplay display = context->display == EGL_NO_DISPLAY ?
        eglGetCurrentDisplay() :
        context->display;

    // If the current display is also EGL_NO_DISPLAY, we assume that there
    // is currently no context set and no action needs to be taken to unset
    // the context.
    if (display == EGL_NO_DISPLAY)
    {
        return true;
    }

    return eglMakeCurrent(display, context->draw_surface, context->read_surface,
        context->context);
}

void wlr_egl_save_context(struct wlr_egl_context *context)
{
    context->display = eglGetCurrentDisplay();
    context->context = eglGetCurrentContext();
    context->draw_surface = eglGetCurrentSurface(EGL_DRAW);
    context->read_surface = eglGetCurrentSurface(EGL_READ);
}

struct sparrow_dmabuf_buffer
{
    struct wlr_addon addon;
    void *egl_image;
    GLuint tex;
};

static void handle_dmabuf_buffer_destroy(struct wlr_addon *addon)
{
    struct sparrow_dmabuf_buffer *buffer = wl_container_of(addon, buffer, addon);
    Core *instance = Core::instance();
    struct sparrow_renderer *renderer =
        instance ? &instance->sparrow_renderer : nullptr;

    if (renderer)
    {
        pthread_mutex_lock(&renderer->texture_mutex);
    }

    wlr_addon_finish(&buffer->addon);

    if (instance && instance->egl_display)
    {
        EGLDisplay display   = instance->egl_display;
        EGLContext prev_ctx  = eglGetCurrentContext();
        EGLSurface prev_draw = eglGetCurrentSurface(EGL_DRAW);
        EGLSurface prev_read = eglGetCurrentSurface(EGL_READ);

        EGLContext resource_ctx =
            renderer ? renderer->main_thread_egl_context : EGL_NO_CONTEXT;
        if ((resource_ctx != EGL_NO_CONTEXT) && (prev_ctx != resource_ctx))
        {
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, resource_ctx);
        }

        if (buffer->tex != 0)
        {
            glDeleteTextures(1, &buffer->tex);
            buffer->tex = 0;
        }

        if ((buffer->egl_image != nullptr) && (instance->egl != nullptr))
        {
            if (renderer && (renderer->eglDestroyImageKHR != nullptr))
            {
                renderer->eglDestroyImageKHR(display, buffer->egl_image);
            } else
            {
    #ifdef USE_GLES32
                instance->egl->procs.eglDestroyImageKHR(display, buffer->egl_image);
    #endif
            }

            buffer->egl_image = nullptr;
        }

        if ((resource_ctx != EGL_NO_CONTEXT) && (prev_ctx != resource_ctx))
        {
            eglMakeCurrent(display, prev_draw, prev_read, prev_ctx);
        }
    }

    if (renderer)
    {
        pthread_mutex_unlock(&renderer->texture_mutex);
    }

    delete buffer;
}

static const struct wlr_addon_interface dmabuf_buffer_addon_impl = {
    .name    = "sparrow_dmabuf_buffer",
    .destroy = handle_dmabuf_buffer_destroy,
};

bool sparrow_renderer_import_dmabuf_buffer(struct wlr_buffer *source_buffer,
    FlutterOpenGLTexture *texture_out)
{
    if (!source_buffer || !texture_out)
    {
        return false;
    }

    Core *instance = Core::instance();
    if (!instance)
    {
        return false;
    }

    struct sparrow_renderer *renderer = &instance->sparrow_renderer;

    if (!renderer->has_dmabuf_import)
    {
        return false;
    }

    // Try to get DMA-BUF attributes from the source buffer
    struct wlr_dmabuf_attributes dmabuf_attribs;
    if (!wlr_buffer_get_dmabuf(source_buffer, &dmabuf_attribs))
    {
        return false;
    }

    // Check if buffer is already imported and cached via wlr_addon
    struct wlr_addon *addon = wlr_addon_find(&source_buffer->addons, instance,
                                             &dmabuf_buffer_addon_impl);
    if (addon != nullptr)
    {
        struct sparrow_dmabuf_buffer *buf = wl_container_of(addon, buf, addon);
        texture_out->target = GL_TEXTURE_2D;
        texture_out->name   = buf->tex;
    #ifdef USE_GLES32
        texture_out->format = GL_RGBA8;
    #else
        texture_out->format = GL_RGBA8_OES;
    #endif
        texture_out->user_data = nullptr;
        texture_out->destruction_callback = nullptr;
        texture_out->width  = dmabuf_attribs.width;
        texture_out->height = dmabuf_attribs.height;
        return true;
    }

    // Get or create EGLImage for this buffer
    void *egl_image = get_or_create_egl_image(&dmabuf_attribs);
    if (egl_image == nullptr)
    {
        wlr_log(WLR_DEBUG,
            "Failed to create EGLImage from DMA-BUF (format=0x%x, mod=0x%lx)",
            dmabuf_attribs.format, dmabuf_attribs.modifier);
        return false;
    }

// Create GL texture
    #ifndef USE_GLES32
    struct gl_fns *fns = &renderer->fns;
    GLuint tex = 0;
    if (fns->glGenTextures != nullptr)
    {
        fns->glGenTextures(1, &tex);
    } else
    {
        glGenTextures(1, &tex);
    }

    if (tex == 0)
    {
        wlr_log(WLR_ERROR, "glGenTextures failed in dmabuf import");
        return false;
    }

    GLenum target = GL_TEXTURE_2D;

    if (fns->glBindTexture != nullptr)
    {
        fns->glBindTexture(target, tex);
    } else
    {
        glBindTexture(target, tex);
    }

    renderer->glEGLImageTargetTexture2DOES(target, egl_image);

    if (fns->glTexParameteri != nullptr)
    {
        fns->glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        fns->glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        fns->glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        fns->glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        fns->glBindTexture(target, 0);
    } else
    {
        glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(target, 0);
    }

    #else
    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (tex == 0)
    {
        wlr_log(WLR_ERROR, "glGenTextures failed in dmabuf import");
        return false;
    }

    GLenum target = GL_TEXTURE_2D;

    glBindTexture(target, tex);
    renderer->glEGLImageTargetTexture2DOES(target, egl_image);

    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(target, 0);
    #endif

    struct sparrow_dmabuf_buffer *buf = new sparrow_dmabuf_buffer();
    if (buf == nullptr)
    {
    #ifndef USE_GLES32
        if (fns->glDeleteTextures != nullptr)
        {
            fns->glDeleteTextures(1, &tex);
        } else
        {
            glDeleteTextures(1, &tex);
        }

    #else
        glDeleteTextures(1, &tex);
    #endif
        return false;
    }

    buf->egl_image = egl_image;
    buf->tex = tex;
    wlr_addon_init(&buf->addon, &source_buffer->addons, instance,
        &dmabuf_buffer_addon_impl);

    texture_out->target = GL_TEXTURE_2D;
    texture_out->name   = tex;
    #ifdef USE_GLES32
    texture_out->format = GL_RGBA8;
    #else
    texture_out->format = GL_RGBA8_OES;
    #endif
    texture_out->user_data = nullptr;
    texture_out->destruction_callback = nullptr;
    texture_out->width  = dmabuf_attribs.width;
    texture_out->height = dmabuf_attribs.height;

    return true;
}

bool sparrow_renderer_import_surface_dmabuf(const struct wlr_surface *surface,
    FlutterOpenGLTexture *texture_out)
{
    if (!surface || !surface->buffer || !surface->buffer->source)
    {
        return false;
    }

    return sparrow_renderer_import_dmabuf_buffer(surface->buffer->source,
        texture_out);
}

#endif

static void render_scene_layer_texture(struct wlr_render_pass *render_pass,
    struct sparrow_renderer_scene_layer *layer,
    struct sparrow_output_viewport *viewport,
    pixman_region32_t *damage_region = nullptr)
{
    SPARROW_GL_SCOPE("FlutterUI::BackingStoreLayer");
    Core *instance = Core::instance();

    struct sparrow_renderer *renderer = &instance->sparrow_renderer;

    // We are on GLES2 backend, so we can cast wlr_renderer to access internal
    // GLES2 functions if needed However, wlroots 0.18 prefers buffer imports.
    // Since we don't have easy DMA-BUF export here without more plumbing,
    // we will stick to the GL path for now but fix the coordinates.
#ifndef USE_GLES32
    const struct gl_fns *fns = &renderer->fns;
#endif

    // Use the specific output's viewport for coordinate mapping
    // Each output shows only its portion of the total Flutter surface
    const int output_width  = viewport->width > 0 ? viewport->width : 1;
    const int output_height = viewport->height > 0 ? viewport->height : 1;
    const int buffer_width  =
        viewport->buffer_width > 0 ? viewport->buffer_width : output_width;
    const int buffer_height =
        viewport->buffer_height > 0 ? viewport->buffer_height : output_height;

    // Snap layer bounds to integer pixels to match platform-view rendering.
    // This prevents subtle frame/content desync while moving windows.
    // Apply viewport offset to get coordinates relative to this output
    const double x0 = floor(layer->offset.x) - viewport->x;
    const double y0 = floor(layer->offset.y) - viewport->y;
    const double x1 = ceil(layer->offset.x + layer->size.width) - viewport->x;
    const double y1 = ceil(layer->offset.y + layer->size.height) - viewport->y;

#ifdef DAMAGE_HISTORY
    if ((damage_region != nullptr) && pixman_region32_not_empty(damage_region))
    {
        pixman_region32_t test_reg;
        pixman_region32_init_rect(&test_reg, (int)x0, (int)y0, (int)(x1 - x0),
            (int)(y1 - y0));
        pixman_region32_intersect(&test_reg, &test_reg, damage_region);
        bool intersects = pixman_region32_not_empty(&test_reg);
        pixman_region32_fini(&test_reg);
        if (!intersects)
        {
            return; // Skip rendering this backing store layer completely!
        }
    }

#endif

    struct wlr_box log_box = {
        .x     = (int)x0,
        .y     = (int)y0,
        .width = (int)(x1 - x0),
        .height = (int)(y1 - y0),
    };

    struct wlr_box phys_box = log_box;
    if (viewport->transform != WL_OUTPUT_TRANSFORM_NORMAL)
    {
        wlr_box_transform(&phys_box, &log_box, viewport->transform, output_width,
            output_height);
    }

    // Normalize coordinates to -1.0 to 1.0 (NDC) on the physical output buffer
    const float left =
        (float)(((double)phys_box.x / (double)buffer_width) * 2.0 - 1.0);
    const float right =
        (float)(((double)(phys_box.x + phys_box.width) / (double)buffer_width) *
            2.0 -
            1.0);
    const float top =
        (float)(1.0 - ((double)phys_box.y / (double)buffer_height) * 2.0);
    const float bottom = (float)(1.0 - ((double)(phys_box.y + phys_box.height) /
        (double)buffer_height) *
        2.0);

    const GLfloat quad_verts_local[8] = {
        right, bottom, // Bottom-right
        left, bottom, // Bottom-left
        right, top, // Top-right
        left, top, // Top-left
    };

    GLfloat tex_coords_local[8];
    get_texcoords_for_transform(viewport->transform, tex_coords_local);

#ifdef GL_DEGUB
    fns->glUseProgram(renderer->quad_rgbx_shader.prog);

    fns->glEnableVertexAttribArray(renderer->quad_rgbx_shader.pos_attrib);
    fns->glBindBuffer(GL_ARRAY_BUFFER, renderer->quad_vert_buffer);
    fns->glBufferData(GL_ARRAY_BUFFER, sizeof(quad_verts_local), quad_verts_local,
        GL_STREAM_DRAW);
    fns->glVertexAttribPointer(renderer->quad_rgbx_shader.pos_attrib, 2, GL_FLOAT,
        GL_FALSE, 0, (void*)0);

    fns->glEnableVertexAttribArray(renderer->quad_rgbx_shader.tex_attrib);
    fns->glBindBuffer(GL_ARRAY_BUFFER, renderer->tex_coord_buffer);
    fns->glBufferData(GL_ARRAY_BUFFER, sizeof(tex_coords_local), tex_coords_local,
        GL_STREAM_DRAW);
    fns->glVertexAttribPointer(renderer->quad_rgbx_shader.tex_attrib, 2, GL_FLOAT,
        GL_FALSE, 0, (void*)0);

    fns->glActiveTexture(GL_TEXTURE0);

    fns->glBindTexture(GL_TEXTURE_2D, layer->texture.texture->texture);
    fns->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    fns->glDisableVertexAttribArray(renderer->quad_rgbx_shader.pos_attrib);
    fns->glDisableVertexAttribArray(renderer->quad_rgbx_shader.tex_attrib);

    fns->glBindTexture(GL_TEXTURE_2D, 0);
    fns->glBindBuffer(GL_ARRAY_BUFFER, 0);

    fns->glUseProgram(0);
#else
    glUseProgram(renderer->quad_rgbx_shader.prog);

    glEnableVertexAttribArray(renderer->quad_rgbx_shader.pos_attrib);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->quad_vert_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_verts_local), quad_verts_local,
        GL_STREAM_DRAW);
    glVertexAttribPointer(renderer->quad_rgbx_shader.pos_attrib, 2, GL_FLOAT,
        GL_FALSE, 0, (void*)0);

    glEnableVertexAttribArray(renderer->quad_rgbx_shader.tex_attrib);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->tex_coord_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tex_coords_local), tex_coords_local,
        GL_STREAM_DRAW);
    glVertexAttribPointer(renderer->quad_rgbx_shader.tex_attrib, 2, GL_FLOAT,
        GL_FALSE, 0, (void*)0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, layer->texture.texture->texture);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(renderer->quad_rgbx_shader.pos_attrib);
    glDisableVertexAttribArray(renderer->quad_rgbx_shader.tex_attrib);

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glUseProgram(0);
#endif
}

void sparrow_renderer_render_scene(struct wlr_render_pass *render_pass,
    struct sparrow_output_viewport *viewport,
    pixman_region32_t *damage_region)
{
    Core *instance = Core::instance();

    struct sparrow_renderer *renderer = &instance->sparrow_renderer;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    pthread_mutex_lock(&renderer->render_mutex);

    if (renderer->current_scene.sync != 0)
    {
        eglWaitSync(instance->egl_display, renderer->current_scene.sync, 0);
        eglDestroySync(instance->egl_display, renderer->current_scene.sync);
        renderer->current_scene.sync = 0;
    }

    for (int i = 0; i < (int)renderer->current_scene.layers_count; i++)
    {
        struct sparrow_renderer_scene_layer *layer =
            &renderer->current_scene.layers[i];
        switch (layer->type)
        {
          case sceneLayerPlatform:
            render_scene_layer_platform(render_pass, layer, &now, viewport,
                damage_region);
            break;

          case sceneLayerTexture:
            render_scene_layer_texture(render_pass, layer, viewport, damage_region);
            break;
        }
    }

    renderer->current_scene.needs_update = false;

    pthread_mutex_unlock(&renderer->render_mutex);
}

void sparrow_renderer_update_scene_positions()
{
    Core *instance = Core::instance();

    if (!instance || (instance->scene == nullptr))
    {
        return;
    }

    // Scene node enable/disable is managed by map/unmap handlers.
    // Here we just sync positions with Flutter's view positions.
    // Views rendered via external textures won't have platform layers,
    // so we update all mapped views directly.

    SparrowView *view = nullptr;
    wl_list_for_each(view, &instance->views_list, link)
    {
        if (!view || (view->scene_tree == nullptr))
        {
            continue;
        }

        // Position is set by surface_set_position platform channel message
        // Note: wlr_scene_xdg_surface handles geometry offset internally
        const int titlebar_offset = 0;
        wlr_scene_node_set_position(&view->scene_tree->node, view->x,
            view->y + titlebar_offset);
    }
}

void sparrow_renderer_destroy()
{
    Core *instance = Core::instance();

    if (!instance)
    {
        return;
    }

    struct sparrow_renderer *renderer = &instance->sparrow_renderer;

    // Free all page textures from both pages
    for (int p = 0; p < SPARROW_RENDERER_NUM_PAGES; p++)
    {
        struct sparrow_renderer_page_texture *pt, *tmp;
        wl_list_for_each_safe(pt, tmp, &renderer->pages[p].textures, link)
        {
            if (pt->texture)
            {
                glDeleteTextures(1, &pt->texture);
                pt->texture = 0;
            }

            if (pt->rbo)
            {
                glDeleteRenderbuffers(1, &pt->rbo);
                pt->rbo = 0;
            }

            if (pt->fbo)
            {
                glDeleteFramebuffers(1, &pt->fbo);
                pt->fbo = 0;
            }

            wl_list_remove(&pt->link);
            delete pt;
        }
        wl_list_init(&renderer->pages[p].textures);
    }

    // Free scene layers
    for (int i = 0; i < (int)renderer->current_scene.layers_count; i++)
    {
        struct sparrow_renderer_scene_layer *layer =
            &renderer->current_scene.layers[i];
        if ((layer->type == sceneLayerPlatform) && layer->platform.mutations)
        {
            free(layer->platform.mutations);
            layer->platform.mutations = nullptr;
        }
    }

    if (renderer->current_scene.layers)
    {
        free(renderer->current_scene.layers);
        renderer->current_scene.layers = nullptr;
        renderer->current_scene.layers_count = 0;
    }

    // Destroy EGL sync if present
    if (renderer->current_scene.sync != 0)
    {
        eglDestroySync(instance->egl_display, renderer->current_scene.sync);
        renderer->current_scene.sync = 0;
    }

    // Delete quad vert and tex coord buffers
    if (renderer->quad_vert_buffer != 0)
    {
        glDeleteBuffers(1, &renderer->quad_vert_buffer);
        renderer->quad_vert_buffer = 0;
    }

    if (renderer->tex_coord_buffer != 0)
    {
        glDeleteBuffers(1, &renderer->tex_coord_buffer);
        renderer->tex_coord_buffer = 0;
    }

    // Destroy render mutex
    pthread_mutex_destroy(&renderer->render_mutex);
    pthread_mutex_destroy(&renderer->texture_mutex);

    // Unbind
    eglMakeCurrent(instance->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
        EGL_NO_CONTEXT);

    // Destroy contexts
    if (renderer->main_thread_egl_context != EGL_NO_CONTEXT)
    {
        eglDestroyContext(instance->egl_display, renderer->main_thread_egl_context);
        renderer->main_thread_egl_context = EGL_NO_CONTEXT;
    }

    if (renderer->flutter_resource_egl_context != EGL_NO_CONTEXT)
    {
        eglDestroyContext(instance->egl_display,
            renderer->flutter_resource_egl_context);
        renderer->flutter_resource_egl_context = EGL_NO_CONTEXT;
    }

    if (renderer->flutter_egl_context != EGL_NO_CONTEXT)
    {
        eglDestroyContext(instance->egl_display, renderer->flutter_egl_context);
        renderer->flutter_egl_context = EGL_NO_CONTEXT;
    }
}
