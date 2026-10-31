#ifndef SPARROW_TRACE_HPP
#define SPARROW_TRACE_HPP

#if defined (SPARROW_ENABLE_TRACE)

    #include <perfetto.h>
    #include <EGL/egl.h>
    #include <string>
    #include <memory>
    #include <cstdint>

    #ifdef USE_GLES32
        #include <GLES3/gl32.h>
    #else
        #include <GLES2/gl2.h>
        #include <GLES2/gl2ext.h>
    #endif

// Define Perfetto categories in header
PERFETTO_DEFINE_CATEGORIES(
    perfetto::Category("gpu").SetDescription("GPU and OpenGL rendering passes"),
    perfetto::Category("compositor").SetDescription("Sparrow Wayland compositor lifecycle"),
    perfetto::Category("render").SetDescription("Flutter UI rendering and rasterization"),
    perfetto::Category("ipc").SetDescription("Zero-Trust IPC communication"));

// Function pointer typedefs for GL Debug extensions
    #ifndef GL_KHR_debug
typedef void (*PFNGLPUSHDEBUGGROUPPROC)(GLenum source, GLuint id, GLsizei length, const GLchar *message);
typedef void (*PFNGLPOPDEBUGGROUPPROC)(void);
typedef void (*PFNGLOBJECTLABELPROC)(GLenum identifier, GLuint name, GLsizei length, const GLchar *label);
        #define GL_DEBUG_SOURCE_APPLICATION 0x824A
        #define GL_BUFFER 0x82E0
        #define GL_TEXTURE 0x1702
        #define GL_FRAMEBUFFER 0x8D40
    #endif

    #ifndef GL_EXT_debug_marker
typedef void (*PFNGLPUSHGROUPMARKEREXTPROC)(GLsizei length, const GLchar *marker);
typedef void (*PFNGLPOPGROUPMARKEREXTPROC)(void);
typedef void (*PFNGLINSERTEVENTMARKEREXTPROC)(GLsizei length, const GLchar *marker);
    #endif

class SparrowTrace
{
  public:
    static SparrowTrace& instance();

    void init(const char *perfetto_out_path = nullptr, int rdoc_frame = -1);
    void finish();

    void init_gl();
    void gl_push_group(const char *name);
    void gl_pop_group();
    void gl_set_object_label(GLenum type, GLuint object, const char *label);

    bool is_gl_tracing_enabled() const;

    // RenderDoc in-app frame capture
    bool has_renderdoc() const;
    void trigger_renderdoc_capture();
    void set_target_renderdoc_frame(uint32_t frame_index);
    void start_renderdoc_frame_if_requested(uint32_t current_frame);
    void end_renderdoc_frame_if_capturing(uint32_t current_frame);

    // Perfetto session control
    bool start_perfetto_session(const std::string& path = "");
    bool stop_perfetto_session();
    bool toggle_perfetto_session();
    bool is_perfetto_active() const;

  private:
    SparrowTrace();
    ~SparrowTrace();

    bool initialized_    = false;
    bool gl_initialized_ = false;
    bool gl_tracing_enabled_ = false;

    PFNGLPUSHDEBUGGROUPPROC glPushDebugGroup_ = nullptr;
    PFNGLPOPDEBUGGROUPPROC glPopDebugGroup_   = nullptr;
    PFNGLOBJECTLABELPROC glObjectLabel_ = nullptr;
    PFNGLPUSHGROUPMARKEREXTPROC glPushGroupMarkerEXT_ = nullptr;
    PFNGLPOPGROUPMARKEREXTPROC glPopGroupMarkerEXT_   = nullptr;

    bool rdoc_available_    = false;
    bool rdoc_capture_next_ = false;
    bool rdoc_capturing_    = false;
    int32_t rdoc_target_frame_ = -1;

    std::string perfetto_file_path_ = "out/sparrow.pftrace";
    std::unique_ptr<perfetto::TracingSession> perfetto_session_;
};

// RAII Scope for GLES Debug Markers
class SparrowGLDebugScope
{
  public:
    explicit SparrowGLDebugScope(const char *name)
    {
        SparrowTrace::instance().gl_push_group(name);
    }

    ~SparrowGLDebugScope()
    {
        SparrowTrace::instance().gl_pop_group();
    }
};

// Macros when SPARROW_ENABLE_TRACE is active
    #define SPARROW_TRACE_SCOPE_1(name) TRACE_EVENT("gpu", name)
    #define SPARROW_TRACE_SCOPE_2(cat, name) TRACE_EVENT(cat, name)
    #define GET_TRACE_MACRO(_1, _2, NAME, ...) NAME
    #define SPARROW_TRACE_SCOPE(...) \
            GET_TRACE_MACRO(__VA_ARGS__, SPARROW_TRACE_SCOPE_2, \
    SPARROW_TRACE_SCOPE_1)(__VA_ARGS__)

    #define SPARROW_TRACE_INSTANT(category, name) TRACE_EVENT_INSTANT(category, name)
    #define SPARROW_GL_SCOPE(name) SparrowGLDebugScope _gl_scope_ ## __COUNTER__(name)

    #define SPARROW_GPU_SCOPE(name) \
            TRACE_EVENT("gpu", name); \
            SparrowGLDebugScope _gl_scope_ ## __COUNTER__(name)

    #define SPARROW_CPU_SCOPE(name) TRACE_EVENT("compositor", name)

    #define SPARROW_GL_LABEL_TEXTURE(tex, label) \
            SparrowTrace::instance().gl_set_object_label(GL_TEXTURE, tex, label)

    #define SPARROW_GL_LABEL_FBO(fbo, label) \
            SparrowTrace::instance().gl_set_object_label(GL_FRAMEBUFFER, fbo, label)

    #define SPARROW_RENDERDOC_TRIGGER_CAPTURE() \
            SparrowTrace::instance().trigger_renderdoc_capture()

#else // !SPARROW_ENABLE_TRACE (Release / Zero Overhead)

// Strict zero-overhead empty macros in release mode
    #define SPARROW_TRACE_SCOPE(...) do {} while (0)
    #define SPARROW_TRACE_INSTANT(...) do {} while (0)
    #define SPARROW_GL_SCOPE(...) do {} while (0)
    #define SPARROW_GPU_SCOPE(...) do {} while (0)
    #define SPARROW_CPU_SCOPE(...) do {} while (0)
    #define SPARROW_GL_LABEL_TEXTURE(...) do {} while (0)
    #define SPARROW_GL_LABEL_FBO(...) do {} while (0)
    #define SPARROW_RENDERDOC_TRIGGER_CAPTURE() do {} while (0)

#endif // SPARROW_ENABLE_TRACE

#endif // SPARROW_TRACE_HPP
