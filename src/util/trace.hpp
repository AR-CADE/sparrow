#ifndef SPARROW_TRACE_HPP
#define SPARROW_TRACE_HPP

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <EGL/egl.h>

#ifdef USE_GLES32
    #include <GLES3/gl32.h>
#else
    #include <GLES2/gl2.h>
    #include <GLES2/gl2ext.h>
#endif

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
    static SparrowTrace&instance()
    {
        static SparrowTrace s_instance;
        return s_instance;
    }

    bool is_enabled()
    {
        if (!config_checked_)
        {
            config_checked_ = true;
            const char *tg = getenv("SPARROW_TRACE_GPU");
            const char *tr = getenv("SPARROW_TRACE");
            const char *rd = getenv("RENDERDOC_CAPFILE");
            enabled_ = (tg && (strcmp(tg, "1") == 0 || strcasecmp(tg, "true") == 0)) ||
                (tr && (strcmp(tr, "1") == 0 || strcasecmp(tr, "true") == 0)) ||
                (rd != nullptr);
        }

        return enabled_;
    }

    void init_gl()
    {
        if (gl_initialized_)
        {
            return;
        }

        gl_initialized_ = true;
        if (!is_enabled())
        {
            return;
        }

        // Check for GLES 3.2 or KHR_debug
        glPushDebugGroup_ = (PFNGLPUSHDEBUGGROUPPROC)eglGetProcAddress("glPushDebugGroup");
        if (!glPushDebugGroup_)
        {
            glPushDebugGroup_ = (PFNGLPUSHDEBUGGROUPPROC)eglGetProcAddress("glPushDebugGroupKHR");
        }

        glPopDebugGroup_ = (PFNGLPOPDEBUGGROUPPROC)eglGetProcAddress("glPopDebugGroup");
        if (!glPopDebugGroup_)
        {
            glPopDebugGroup_ = (PFNGLPOPDEBUGGROUPPROC)eglGetProcAddress("glPopDebugGroupKHR");
        }

        glObjectLabel_ = (PFNGLOBJECTLABELPROC)eglGetProcAddress("glObjectLabel");
        if (!glObjectLabel_)
        {
            glObjectLabel_ = (PFNGLOBJECTLABELPROC)eglGetProcAddress("glObjectLabelKHR");
        }

        // Fallback: EXT_debug_marker
        glPushGroupMarkerEXT_ = (PFNGLPUSHGROUPMARKEREXTPROC)eglGetProcAddress("glPushGroupMarkerEXT");
        glPopGroupMarkerEXT_  = (PFNGLPOPGROUPMARKEREXTPROC)eglGetProcAddress("glPopGroupMarkerEXT");
    }

    void init_ftrace()
    {
        if (ftrace_initialized_)
        {
            return;
        }

        ftrace_initialized_ = true;
        if (!is_enabled())
        {
            return;
        }

        pid_ = getpid();
        // Try standard trace_marker paths for Perfetto / FTrace
        ftrace_fd_ = open("/sys/kernel/tracing/trace_marker", O_WRONLY | O_CLOEXEC);
        if (ftrace_fd_ < 0)
        {
            ftrace_fd_ = open("/sys/kernel/debug/tracing/trace_marker", O_WRONLY | O_CLOEXEC);
        }
    }

    void gl_push_group(const char *name)
    {
        if (!name || !is_enabled())
        {
            return;
        }

        if (!gl_initialized_)
        {
            init_gl();
        }

        if (glPushDebugGroup_)
        {
            glPushDebugGroup_(GL_DEBUG_SOURCE_APPLICATION, 0, -1, name);
        } else if (glPushGroupMarkerEXT_)
        {
            glPushGroupMarkerEXT_(0, name);
        }
    }

    void gl_pop_group()
    {
        if (!is_enabled())
        {
            return;
        }

        if (glPopDebugGroup_)
        {
            glPopDebugGroup_();
        } else if (glPopGroupMarkerEXT_)
        {
            glPopGroupMarkerEXT_();
        }
    }

    void gl_set_object_label(GLenum type, GLuint object, const char *label)
    {
        if (!label || (object == 0) || !is_enabled())
        {
            return;
        }

        if (!gl_initialized_)
        {
            init_gl();
        }

        if (glObjectLabel_)
        {
            glObjectLabel_(type, object, -1, label);
        }
    }

    void trace_begin(const char *name)
    {
        if (!name || !is_enabled())
        {
            return;
        }

        if (!ftrace_initialized_)
        {
            init_ftrace();
        }

        if (ftrace_fd_ >= 0)
        {
            char buf[256];
            int len = snprintf(buf, sizeof(buf), "B|%d|%s", pid_, name);
            if (len > 0)
            {
                ssize_t ret = write(ftrace_fd_, buf, len);
                (void)ret;
            }
        }
    }

    void trace_end()
    {
        if (!is_enabled())
        {
            return;
        }

        if (ftrace_fd_ >= 0)
        {
            char buf[32];
            int len = snprintf(buf, sizeof(buf), "E|%d", pid_);
            if (len > 0)
            {
                ssize_t ret = write(ftrace_fd_, buf, len);
                (void)ret;
            }
        }
    }

    ~SparrowTrace()
    {
        if (ftrace_fd_ >= 0)
        {
            close(ftrace_fd_);
            ftrace_fd_ = -1;
        }
    }

  private:
    SparrowTrace() = default;

    bool config_checked_ = false;
    bool enabled_ = false;
    bool gl_initialized_ = false;
    PFNGLPUSHDEBUGGROUPPROC glPushDebugGroup_ = nullptr;
    PFNGLPOPDEBUGGROUPPROC glPopDebugGroup_   = nullptr;
    PFNGLOBJECTLABELPROC glObjectLabel_ = nullptr;
    PFNGLPUSHGROUPMARKEREXTPROC glPushGroupMarkerEXT_ = nullptr;
    PFNGLPOPGROUPMARKEREXTPROC glPopGroupMarkerEXT_   = nullptr;

    bool ftrace_initialized_ = false;
    int ftrace_fd_ = -1;
    pid_t pid_     = 0;
};

// RAII Scope for GPU Debug Markers (RenderDoc, Mali, Nsight)
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

// RAII Scope for Perfetto / FTrace / ATrace
class SparrowCPUDebugScope
{
  public:
    explicit SparrowCPUDebugScope(const char *name)
    {
        SparrowTrace::instance().trace_begin(name);
    }

    ~SparrowCPUDebugScope()
    {
        SparrowTrace::instance().trace_end();
    }
};

// Combined GPU + CPU Trace Scope
class SparrowTraceScope
{
  public:
    explicit SparrowTraceScope(const char *name)
    {
        SparrowTrace::instance().trace_begin(name);
        SparrowTrace::instance().gl_push_group(name);
    }

    ~SparrowTraceScope()
    {
        SparrowTrace::instance().gl_pop_group();
        SparrowTrace::instance().trace_end();
    }
};

#define SPARROW_GL_SCOPE(name) SparrowGLDebugScope _gl_scope_ ## __COUNTER__(name)
#define SPARROW_CPU_SCOPE(name) SparrowCPUDebugScope _cpu_scope_ ## __COUNTER__(name)
#define SPARROW_TRACE_SCOPE(name) SparrowTraceScope _trace_scope_ ## __COUNTER__(name)

#define SPARROW_GL_LABEL_TEXTURE(tex, label) \
        SparrowTrace::instance().gl_set_object_label(GL_TEXTURE, tex, \
    label)
#define SPARROW_GL_LABEL_FBO(fbo, label) \
        SparrowTrace::instance().gl_set_object_label(GL_FRAMEBUFFER, fbo, \
    label)

#endif // SPARROW_TRACE_HPP
