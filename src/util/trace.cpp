#include "util/trace.hpp"

#if defined (SPARROW_ENABLE_TRACE)

    #include "util/renderdoc_app.h"
    #include <dlfcn.h>
    #include <fstream>
    #include <iostream>
    #include <sparrow/nonstd/wlroots-full.hpp>

PERFETTO_TRACK_EVENT_STATIC_STORAGE();

static RENDERDOC_API_1_5_0 *s_rdoc_api = nullptr;

SparrowTrace& SparrowTrace::instance()
{
    static SparrowTrace s_instance;
    return s_instance;
}

SparrowTrace::SparrowTrace() = default;

SparrowTrace::~SparrowTrace()
{
    finish();
}

void SparrowTrace::init(const char *perfetto_out_path, int rdoc_frame)
{
    if (initialized_)
    {
        return;
    }

    initialized_ = true;

    // 1. Initialize Perfetto SDK in-process
    perfetto::TracingInitArgs args;
    args.backends = perfetto::kInProcessBackend;
    perfetto::Tracing::Initialize(args);
    perfetto::TrackEvent::Register();

    // 2. Initialize RenderDoc in-app API if loaded
    void *mod = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
    if (mod)
    {
        pRENDERDOC_GetAPI get_api = (pRENDERDOC_GetAPI)dlsym(mod, "RENDERDOC_GetAPI");
        if (get_api)
        {
            get_api(eRENDERDOC_API_Version_1_5_0, (void**)&s_rdoc_api);
            if (s_rdoc_api)
            {
                int major = 0, minor = 0, patch = 0;
                s_rdoc_api->GetAPIVersion(&major, &minor, &patch);
                rdoc_available_ = true;
                wlr_log(WLR_INFO, "[RENDERDOC] In-application API attached (v%d.%d.%d)", major, minor, patch);
            }
        }
    }

    if (rdoc_frame >= 0)
    {
        rdoc_target_frame_ = rdoc_frame;
        wlr_log(WLR_INFO, "[RENDERDOC] Scheduled automatic capture for frame %d", rdoc_frame);
    }

    // 3. Resolve Perfetto output path from arg or env
    if (!perfetto_out_path || (strlen(perfetto_out_path) == 0))
    {
        perfetto_out_path = getenv("SPARROW_TRACE_PERFETTO");
        if (!perfetto_out_path)
        {
            const char *tr = getenv("SPARROW_TRACE");
            if (tr && ((strcmp(tr, "1") == 0) || (strcasecmp(tr, "true") == 0)))
            {
                perfetto_out_path = "out/sparrow.pftrace";
            }
        }
    }

    if (perfetto_out_path && (strlen(perfetto_out_path) > 0))
    {
        start_perfetto_session(perfetto_out_path);
    }
}

void SparrowTrace::finish()
{
    if (perfetto_session_)
    {
        stop_perfetto_session();
    }
}

bool SparrowTrace::start_perfetto_session(const std::string& path)
{
    if (perfetto_session_)
    {
        return false;
    }

    if (!path.empty())
    {
        perfetto_file_path_ = path;
    }

    if (perfetto_file_path_.empty())
    {
        perfetto_file_path_ = "out/sparrow.pftrace";
    }

    perfetto::TraceConfig cfg;
    cfg.add_buffers()->set_size_kb(64 * 1024); // 64 MB in-memory buffer
    auto *ds_cfg = cfg.add_data_sources()->mutable_config();
    ds_cfg->set_name("track_event");

    perfetto_session_ = perfetto::Tracing::NewTrace();
    perfetto_session_->Setup(cfg);
    perfetto_session_->StartBlocking();
    wlr_log(WLR_INFO, "[PERFETTO] In-process trace recording started (target: %s)",
        perfetto_file_path_.c_str());
    return true;
}

bool SparrowTrace::stop_perfetto_session()
{
    if (!perfetto_session_)
    {
        return false;
    }

    perfetto::TrackEvent::Flush();
    perfetto_session_->StopBlocking();
    std::vector<char> trace_data(perfetto_session_->ReadTraceBlocking());
    perfetto_session_.reset();

    std::ofstream out(perfetto_file_path_, std::ios::out | std::ios::binary);
    if (out.is_open())
    {
        out.write(trace_data.data(), trace_data.size());
        out.close();
        wlr_log(WLR_INFO,
            "[PERFETTO] Trace successfully written to %s (%zu bytes). Open in https://ui.perfetto.dev",
            perfetto_file_path_.c_str(), trace_data.size());
        return true;
    } else
    {
        wlr_log(WLR_ERROR, "[PERFETTO] Failed to write trace file to %s", perfetto_file_path_.c_str());
        return false;
    }
}

bool SparrowTrace::toggle_perfetto_session()
{
    if (perfetto_session_)
    {
        stop_perfetto_session();
        return false;
    } else
    {
        start_perfetto_session(perfetto_file_path_);
        return true;
    }
}

bool SparrowTrace::is_perfetto_active() const
{
    return perfetto_session_ != nullptr;
}

bool SparrowTrace::has_renderdoc() const
{
    return rdoc_available_;
}

void SparrowTrace::trigger_renderdoc_capture()
{
    if (!rdoc_available_)
    {
        wlr_log(WLR_INFO, "[RENDERDOC] Cannot trigger capture: RenderDoc is not attached");
        return;
    }

    rdoc_capture_next_ = true;
    wlr_log(WLR_INFO, "[RENDERDOC] Capture queued for next frame");
}

void SparrowTrace::set_target_renderdoc_frame(uint32_t frame_index)
{
    rdoc_target_frame_ = (int32_t)frame_index;
}

void SparrowTrace::start_renderdoc_frame_if_requested(uint32_t current_frame)
{
    if (!s_rdoc_api)
    {
        return;
    }

    if (rdoc_capture_next_ || ((rdoc_target_frame_ >= 0) && ((uint32_t)rdoc_target_frame_ == current_frame)))
    {
        s_rdoc_api->StartFrameCapture(nullptr, nullptr);
        rdoc_capturing_    = true;
        rdoc_capture_next_ = false;
        wlr_log(WLR_INFO, "[RENDERDOC] Started frame capture (frame %u)...", current_frame);
    }
}

void SparrowTrace::end_renderdoc_frame_if_capturing(uint32_t current_frame)
{
    if (s_rdoc_api && rdoc_capturing_)
    {
        s_rdoc_api->EndFrameCapture(nullptr, nullptr);
        rdoc_capturing_ = false;
        wlr_log(WLR_INFO, "[RENDERDOC] Finished frame capture (frame %u)", current_frame);
    }
}

void SparrowTrace::init_gl()
{
    if (gl_initialized_)
    {
        return;
    }

    gl_initialized_ = true;

    const char *tg = getenv("SPARROW_TRACE_GPU");
    const char *rd = getenv("RENDERDOC_CAPFILE");
    gl_tracing_enabled_ = (tg && ((strcmp(tg, "1") == 0) || (strcasecmp(tg, "true") == 0))) ||
        (rd != nullptr) || rdoc_available_;

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

    glPushGroupMarkerEXT_ = (PFNGLPUSHGROUPMARKEREXTPROC)eglGetProcAddress("glPushGroupMarkerEXT");
    glPopGroupMarkerEXT_  = (PFNGLPOPGROUPMARKEREXTPROC)eglGetProcAddress("glPopGroupMarkerEXT");
}

bool SparrowTrace::is_gl_tracing_enabled() const
{
    return gl_tracing_enabled_;
}

void SparrowTrace::gl_push_group(const char *name)
{
    if (!name)
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

void SparrowTrace::gl_pop_group()
{
    if (glPopDebugGroup_)
    {
        glPopDebugGroup_();
    } else if (glPopGroupMarkerEXT_)
    {
        glPopGroupMarkerEXT_();
    }
}

void SparrowTrace::gl_set_object_label(GLenum type, GLuint object, const char *label)
{
    if (!label || (object == 0))
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

#endif // SPARROW_ENABLE_TRACE
