#ifndef RENDERER_H
#define RENDERER_H

#include "shaders.hpp"
#ifdef USE_GLES32
    #include <GLES3/gl32.h>
#else
    #include <GLES2/gl2.h>
    #include <GLES2/gl2ext.h>
#endif
#include <EGL/egl.h>
#include <atomic>
#include <bits/pthreadtypes.h>
#include <pthread.h>

#include <flutter_embedder.h>
#include <sparrow/nonstd/wlroots-full.hpp>
#include <render/egl.h>


struct wlr_gles2_buffer
{
    struct wlr_buffer *buffer = nullptr;
    struct sparrow_renderer *renderer = nullptr;
    struct wl_list link;

    EGLImageKHR image = nullptr;
    GLuint rbo;
    GLuint fbo;

    struct wlr_addon addon;
};

class Core;
struct wlr_render_pass;
struct wlr_scene_buffer;

#ifndef USE_GLES32
struct gl_fns
{
    void (*glGenFramebuffers)(GLsizei, GLuint*);
    void (*glBindFramebuffer)(GLenum, GLuint);
    void (*glGenTextures)(GLsizei, GLuint*);
    void (*glBindTexture)(GLenum, GLuint);
    void (*glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    void (*glTexParameteri)(GLenum, GLenum, GLint);
    void (*glFramebufferTexture)(GLenum, GLenum, GLuint, GLint);
    void (*glDrawBuffers)(GLsizei, const GLenum*);
    GLuint (*glCreateShader)(GLenum);
    void (*glShaderSource)(GLuint, GLsizei, const GLchar**, const GLint*);
    void (*glCompileShader)(GLuint);
    void (*glGetShaderiv)(GLuint, GLenum, GLint*);
    void (*glDeleteShader)(GLuint);
    GLuint (*glCreateProgram)();
    void (*glAttachShader)(GLuint, GLuint);
    void (*glLinkProgram)(GLuint);
    void (*glDetachShader)(GLuint, GLuint);
    void (*glGetProgramiv)(GLuint, GLenum, GLint*);
    void (*glDeleteProgram)(GLuint);
    GLint (*glGetUniformLocation)(GLuint, const GLchar*);
    GLint (*glGetAttribLocation)(GLuint, const GLchar*);
    void (*glActiveTexture)(GLenum);
    void (*glUseProgram)(GLuint);
    void (*glUniformMatrix3fv)(GLint, GLsizei, GLboolean, const GLfloat*);
    void (*glUniform1i)(GLint, GLint);
    void (*glUniform1f)(GLint, GLfloat);
    void (*glUniform2f)(GLint, GLfloat, GLfloat);
    void (*glUniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
    void (*glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
    void (*glEnableVertexAttribArray)(GLuint);
    void (*glDrawArrays)(GLenum, GLint, GLsizei);
    void (*glDisableVertexAttribArray)(GLuint);
    void (*glEnable)(GLenum);
    void (*glDisable)(GLenum);
    void (*glGetTextureImage)(GLuint, GLint, GLenum, GLenum, GLsizei, void*);
    GLenum (*glCheckFramebufferStatus)(GLenum);
    GLenum (*glGetError)();
    void (*glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
    void (*glGenBuffers)(GLsizei, GLuint*);
    void (*glBindBuffer)(GLenum, GLuint);
    void (*glBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
    void (*glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (*glClear)(GLbitfield);
    void (*glDeleteFramebuffers)(GLsizei, GLuint*);
    void (*glDeleteTextures)(GLsizei, GLuint*);
    void (*glBindSampler)(GLuint, GLuint);
    void (*glPushClientAttrib)(GLbitfield);
    void (*glPopClientAttrib)(GLbitfield);
    void (*glBlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum);

    void (*glGetBooleanv)(GLenum, GLboolean*);
    void (*glGetIntegerv)(GLenum, GLint*);
    void (*glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
    void (*glViewport)(GLint, GLint, GLsizei, GLsizei);
    GLboolean (*glIsEnabled)(GLenum);
};

#endif

struct sparrow_renderer_fbo
{
    GLuint fbo;
    GLuint tex;
    EGLSync sync;
};

struct sparrow_renderer_page_texture
{
    struct wl_list link;
    struct sparrow_renderer_page *page = nullptr;
    GLuint texture = 0;
    GLuint fbo     = 0;
    GLuint rbo     = 0;
    size_t width   = 0;
    size_t height  = 0;
};

struct sparrow_renderer_page
{
    // struct wl_list unused_textures;
    struct wl_list textures;
};

enum sparrow_renderer_scene_layer_type
{
    sceneLayerTexture,
    sceneLayerPlatform,
};

struct sparrow_renderer_scene_layer_texture
{
    struct sparrow_renderer_page_texture *texture = nullptr;
};

struct sparrow_renderer_scene_layer_platform
{
    int64_t platform_view_id = 0;
    size_t mutations_count   = 0;
    FlutterPlatformViewMutation *mutations = nullptr;
};

struct sparrow_renderer_scene_layer
{
    FlutterPoint offset;
    FlutterSize size;

    enum sparrow_renderer_scene_layer_type type;
    union
    {
        struct sparrow_renderer_scene_layer_texture texture;
        struct sparrow_renderer_scene_layer_platform platform;
    };
};

struct sparrow_renderer_scene
{
    size_t layers_count = 0;
    struct sparrow_renderer_scene_layer *layers = nullptr;
    EGLSync sync;
    std::atomic<bool> needs_update{false};
};

#define SPARROW_RENDERER_NUM_PAGES 2

struct sparrow_renderer
{
    struct wl_list buffers;
#ifndef USE_GLES32
    struct gl_fns fns = {};
#endif
    GLuint tex_coord_buffer;
    GLuint quad_vert_buffer;

    struct quad_rgbx_shader quad_rgbx_shader;
    struct quad_rounded_shader quad_rounded_shader;
    // struct quad_external_shader quad_external_shader;

    bool fbo_inited     = false;
    uint8_t current_fbo = 0;
    // struct sparrow_renderer_fbo fbos[SPARROW_RENDERER_NUM_FBOS];

    uint8_t current_page = 0;
    struct sparrow_renderer_page pages[SPARROW_RENDERER_NUM_PAGES];
    struct sparrow_renderer_scene current_scene;

    int flutter_tex_width  = 0;
    int flutter_tex_height = 0;

    EGLContext flutter_egl_context = EGL_NO_CONTEXT;
    EGLContext flutter_resource_egl_context = EGL_NO_CONTEXT;
    EGLContext main_thread_egl_context = EGL_NO_CONTEXT;

#ifdef USE_DMABUF
    // EGL DMA-BUF import functions
    void*(*eglCreateImageKHR)(EGLDisplay, EGLContext, unsigned int, void*, const int*) = nullptr;
    int (*eglDestroyImageKHR)(EGLDisplay, void*) = nullptr;
    void (*glEGLImageTargetTexture2DOES)(GLenum, void*) = nullptr;

    bool has_dmabuf_import = false;
#endif
    wlr_egl *egl = nullptr;
    pthread_mutex_t render_mutex;
    pthread_mutex_t texture_mutex; // Protects texture operations across threads
};

typedef void (*gl_resolved_fn)();
typedef gl_resolved_fn (*gl_resolve_fn)(const char *name);

void sparrow_renderer_init(gl_resolve_fn resolver);
// void sparrow_renderer_ensure_fbo(sparrow_instance *instance, int width, int height);
// void sparrow_renderer_render_flutter_buffer(sparrow_instance *instance);
// GLuint sparrow_renderer_get_active_fbo(sparrow_instance *instance);
// void sparrow_renderer_flip_fbo(sparrow_instance *instance);

// Output viewport for multi-monitor and rotated rendering
struct sparrow_output_viewport
{
    int x     = 0;       // Output position in layout
    int y     = 0;
    int width = 0; // Effective logical size (transformed)
    int height = 0;
    int buffer_width  = 0;  // Physical buffer width
    int buffer_height = 0; // Physical buffer height
    enum wl_output_transform transform = WL_OUTPUT_TRANSFORM_NORMAL;
};

void sparrow_renderer_render_scene(struct wlr_render_pass *render_pass,
    struct sparrow_output_viewport *viewport, pixman_region32_t *damage_region = nullptr);
void sparrow_renderer_update_scene_positions();

void sparrow_renderer_destroy();

#ifdef USE_DMABUF
// Import a surface's DMA-BUF into a GL texture for Flutter
// Returns the GL texture ID, or 0 on failure
// Sets cache->is_external if the texture must use GL_TEXTURE_EXTERNAL_OES
struct wlr_surface;
struct wlr_buffer;
bool sparrow_renderer_import_surface_dmabuf(
    const struct wlr_surface *surface,
    FlutterOpenGLTexture *texture_out);
bool sparrow_renderer_import_dmabuf_buffer(
    struct wlr_buffer *source_buffer,
    FlutterOpenGLTexture *texture_out);
#endif

#endif
