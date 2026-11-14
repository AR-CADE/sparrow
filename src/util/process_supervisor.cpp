#include "util/process_supervisor.hpp"
#include "core.hpp"
#include "ipc/ipc_server.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <pthread.h>
#include <sparrow/nonstd/wlroots-full.hpp>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>


static std::string escape_json_str(const char *src)
{
    if (!src)
    {
        return "";
    }

    std::string out;
    out.reserve(strlen(src) + 8);
    for (const char *p = src; *p; ++p)
    {
        switch (*p)
        {
          case '"':
            out += "\\\"";
            break;

          case '\\':
            out += "\\\\";
            break;

          case '\b':
            out += "\\b";
            break;

          case '\f':
            out += "\\f";
            break;

          case '\n':
            out += "\\n";
            break;

          case '\r':
            out += "\\r";
            break;

          case '\t':
            out += "\\t";
            break;

          default:
            if (static_cast<unsigned char>(*p) < 0x20)
            {
                char hex[8];
                snprintf(hex, sizeof(hex), "\\u%04x", static_cast<unsigned char>(*p));
                out += hex;
            } else
            {
                out += *p;
            }

            break;
        }
    }

    return out;
}

ProcessSupervisor::ProcessSupervisor(Core *core) : core_(core)
{}

ProcessSupervisor::~ProcessSupervisor()
{
    shutdown();
}

bool ProcessSupervisor::init(struct wl_event_loop *loop)
{
    if (!loop)
    {
        return false;
    }

    event_loop_ = loop;

    // NOTE: child exits are tracked per-process through a pidfd (see
    // watch_child()). We deliberately do NOT use a SIGCHLD signalfd: it
    // requires SIGCHLD to be blocked in every thread, and a blocked signal mask
    // is inherited across fork()/execve() by every app spawned from the
    // compositor or the Flutter shell (e.g. a terminal would then never be
    // notified that its shell exited).

    return true;
}

void ProcessSupervisor::shutdown()
{
    std::vector<pid_t> active_pids;

    for (auto & proc : processes_)
    {
        unwatch_child(proc.get());

        if (proc->restart_timer)
        {
            wl_event_source_remove(proc->restart_timer);
            proc->restart_timer = nullptr;
        }

        if (proc->pid > 0)
        {
            wlr_log(WLR_INFO,
                "[process-supervisor] Terminating service '%s' (PID %d)",
                proc->id.c_str(), proc->pid);
            proc->manually_stopped = true;
            kill(-proc->pid, SIGTERM);
            kill(proc->pid, SIGTERM);
            active_pids.push_back(proc->pid);
        }
    }

    // Grace period for children to exit cleanly while the Wayland display is
    // still open
    for (int iter = 0; (iter < 30) && !active_pids.empty(); ++iter)
    {
        if (event_loop_)
        {
            wl_event_loop_dispatch(event_loop_, 0);
        }

        for (auto it = active_pids.begin(); it != active_pids.end();)
        {
            int status = 0;
            pid_t r    = waitpid(*it, &status, WNOHANG);
            if ((r == *it) || (r < 0))
            {
                it = active_pids.erase(it);
            } else
            {
                ++it;
            }
        }

        if (!active_pids.empty())
        {
            usleep(10000); // 10ms
        }
    }

    // Force kill any remaining children that didn't terminate in 300ms
    for (pid_t pid : active_pids)
    {
        kill(-pid, SIGKILL);
        kill(pid, SIGKILL);
        int status = 0;
        // SIGKILL cannot be ignored: block until the kernel releases the
        // child so no zombie is left behind.
        waitpid(pid, &status, 0);
    }

    for (const auto & proc : processes_)
    {
        proc->pid   = -1;
        proc->state = ProcessState::Stopped;
    }
}

void ProcessSupervisor::start_all()
{
    if (started_all_)
    {
        return;
    }

    started_all_ = true;

    const char *no_startup = getenv("SPARROW_NO_STARTUP_SCRIPTS");
    if (no_startup && (strcmp(no_startup, "1") == 0))
    {
        wlr_log(WLR_INFO, "[process-supervisor] Startup scripts disabled via "
                          "SPARROW_NO_STARTUP_SCRIPTS=1");
        return;
    }

    wlr_log(WLR_INFO,
        "[process-supervisor] Launching %zu configured startup service(s)...",
        processes_.size());

    for (auto & proc : processes_)
    {
        if (proc->state == ProcessState::Stopped)
        {
            start_process(proc.get());
        }
    }
}

ManagedProcess*ProcessSupervisor::add_or_update_service(
    const std::string & id, const std::string & command, StartupType type)
{
    ManagedProcess *proc = find_by_id(id);
    if (proc)
    {
        proc->command = command;
        proc->type    = type;
        return proc;
    }

    auto new_proc = std::make_unique<ManagedProcess>();
    new_proc->id = id;
    new_proc->command = command;
    new_proc->type    = type;
    new_proc->state   = ProcessState::Stopped;
    new_proc->supervisor = this;

    proc = new_proc.get();
    processes_.push_back(std::move(new_proc));
    wlr_log(
        WLR_INFO, "[process-supervisor] Registered service '%s' (type=%s): %s",
        id.c_str(), (type == StartupType::Restartable ? "restartable" : "normal"),
        command.c_str());

    if (started_all_ && (proc->state == ProcessState::Stopped))
    {
        start_process(proc);
    }

    return proc;
}

ManagedProcess*ProcessSupervisor::find_by_id(const std::string & id)
{
    auto it = std::find_if(processes_.begin(), processes_.end(),
        [id] (const auto & proc) { return proc->id == id; });

    if (it != processes_.end())
    {
        return it.base()->get();
    }

    return nullptr;
}

ManagedProcess*ProcessSupervisor::find_by_pid(pid_t pid)
{
    if (pid <= 0)
    {
        return nullptr;
    }

    for (auto & proc : processes_)
    {
        if (proc->pid == pid)
        {
            return proc.get();
        }
    }

    return nullptr;
}

bool ProcessSupervisor::start_process(ManagedProcess *proc)
{
    if (!proc || proc->command.empty())
    {
        return false;
    }

    if (proc->pid > 0)
    {
        return true;
    }

    if (proc->restart_timer)
    {
        wl_event_source_remove(proc->restart_timer);
        proc->restart_timer = nullptr;
    }

    proc->manually_stopped = false;
    proc->force_restart    = false;
    proc->state = ProcessState::Starting;

    pid_t pid = fork();
    if (pid < 0)
    {
        wlr_log(WLR_ERROR, "[process-supervisor] fork failed for service '%s': %s",
            proc->id.c_str(), strerror(errno));
        proc->state = ProcessState::Crashed;
        notify_state_change(proc);
        return false;
    }

    if (pid == 0)
    {
        setsid();

        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0)
        {
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }

        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGPIPE, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);

        // Signal masks survive execve(): make sure the service starts with a
        // clean mask whatever thread forked it.
        sigset_t unblock_mask;
        sigemptyset(&unblock_mask);
        pthread_sigmask(SIG_SETMASK, &unblock_mask, nullptr);

        execlp("sh", "sh", "-c", proc->command.c_str(),
            static_cast<char*>(nullptr));
        _exit(127);
    }

    struct timeval tv;
    gettimeofday(&tv, nullptr);
    proc->last_start_time_us =
        static_cast<uint64_t>(tv.tv_sec) * 1000000ULL + tv.tv_usec;
    proc->pid   = pid;
    proc->state = ProcessState::Running;

    watch_child(proc);

    wlr_log(WLR_INFO,
        "[process-supervisor] Started service '%s' (PID %d, type=%s): %s",
        proc->id.c_str(), pid,
        (proc->type == StartupType::Restartable ? "restartable" : "normal"),
        proc->command.c_str());

    notify_state_change(proc);
    return true;
}

bool ProcessSupervisor::stop_process(ManagedProcess *proc, int sig)
{
    if (!proc)
    {
        return false;
    }

    proc->manually_stopped = true;
    proc->force_restart    = false;

    if (proc->restart_timer)
    {
        wl_event_source_remove(proc->restart_timer);
        proc->restart_timer = nullptr;
    }

    if (proc->pid > 0)
    {
        wlr_log(
            WLR_INFO,
            "[process-supervisor] Stopping service '%s' (PID %d) with signal %d",
            proc->id.c_str(), proc->pid, sig);
        kill(-proc->pid, sig);
        kill(proc->pid, sig);
        proc->state = ProcessState::Stopped;
    } else
    {
        proc->state = ProcessState::Stopped;
        notify_state_change(proc);
    }

    return true;
}

bool ProcessSupervisor::restart_process(ManagedProcess *proc)
{
    if (!proc)
    {
        return false;
    }

    proc->manually_stopped = false;

    if (proc->restart_timer)
    {
        wl_event_source_remove(proc->restart_timer);
        proc->restart_timer = nullptr;
    }

    if (proc->pid > 0)
    {
        proc->force_restart = true;
        wlr_log(WLR_INFO,
            "[process-supervisor] Requesting restart for running service '%s' "
            "(PID %d)",
            proc->id.c_str(), proc->pid);
        kill(-proc->pid, SIGTERM);
        kill(proc->pid, SIGTERM);
        return true;
    }

    return start_process(proc);
}

static constexpr uint32_t kExitPollIntervalMs = 500;

void ProcessSupervisor::watch_child(ManagedProcess *proc)
{
    if (!proc || (proc->pid <= 0) || !event_loop_)
    {
        return;
    }

    unwatch_child(proc);

    const int fd = static_cast<int>(syscall(SYS_pidfd_open, proc->pid, 0));
    if (fd >= 0)
    {
        proc->pidfd = fd;
        proc->exit_source = wl_event_loop_add_fd(event_loop_, fd,
            WL_EVENT_READABLE, &ProcessSupervisor::handle_pidfd, proc);
        if (proc->exit_source)
        {
            return;
        }

        close(fd);
        proc->pidfd = -1;
    } else if (errno == ESRCH)
    {
        // Already gone (and possibly reaped by someone else): handle now.
        reap_child(proc);
        return;
    }

    // Fallback for kernels without pidfd_open(): poll with WNOHANG.
    wlr_log(WLR_DEBUG,
        "[process-supervisor] pidfd unavailable for '%s' (%s), polling exit",
        proc->id.c_str(), strerror(errno));
    proc->exit_source = wl_event_loop_add_timer(event_loop_,
        &ProcessSupervisor::handle_exit_poll, proc);
    if (proc->exit_source)
    {
        wl_event_source_timer_update(proc->exit_source, kExitPollIntervalMs);
    }
}

void ProcessSupervisor::unwatch_child(ManagedProcess *proc)
{
    if (!proc)
    {
        return;
    }

    if (proc->exit_source)
    {
        wl_event_source_remove(proc->exit_source);
        proc->exit_source = nullptr;
    }

    if (proc->pidfd >= 0)
    {
        close(proc->pidfd);
        proc->pidfd = -1;
    }
}

// Returns true if the child has exited and was handled.
bool ProcessSupervisor::reap_child(ManagedProcess *proc)
{
    if (!proc || (proc->pid <= 0))
    {
        return false;
    }

    const pid_t pid = proc->pid;
    int status = 0;
    pid_t r;
    do {
        r = waitpid(pid, &status, WNOHANG);
    } while ((r < 0) && (errno == EINTR));

    if (r == 0)
    {
        return false; // still running
    }

    if (r < 0)
    {
        // ECHILD: the child was reaped by another waiter in the process
        // (e.g. the Dart VM). Exit status is lost, but the service is gone.
        wlr_log(WLR_INFO,
            "[process-supervisor] Service '%s' (PID %d) was reaped elsewhere (%s)",
            proc->id.c_str(), pid, strerror(errno));
        status = 0;
    }

    unwatch_child(proc);
    on_child_exit(pid, status);
    return true;
}

int ProcessSupervisor::handle_pidfd(int fd, uint32_t mask, void *data)
{
    (void)fd;
    (void)mask;
    ManagedProcess *proc = static_cast<ManagedProcess*>(data);
    if (!proc || !proc->supervisor)
    {
        return 0;
    }

    ProcessSupervisor *self = proc->supervisor;
    if (!self->reap_child(proc))
    {
        // Readable pidfd but child not reapable yet: should not happen, but
        // avoid a level-triggered busy loop by switching to polling.
        self->unwatch_child(proc);
        proc->exit_source = wl_event_loop_add_timer(self->event_loop_,
            &ProcessSupervisor::handle_exit_poll, proc);
        if (proc->exit_source)
        {
            wl_event_source_timer_update(proc->exit_source, kExitPollIntervalMs);
        }
    }

    return 0;
}

int ProcessSupervisor::handle_exit_poll(void *data)
{
    ManagedProcess *proc = static_cast<ManagedProcess*>(data);
    if (!proc || !proc->supervisor)
    {
        return 0;
    }

    if (!proc->supervisor->reap_child(proc) && proc->exit_source)
    {
        wl_event_source_timer_update(proc->exit_source, kExitPollIntervalMs);
    }

    return 0;
}

void ProcessSupervisor::on_child_exit(pid_t pid, int status)
{
    ManagedProcess *proc = find_by_pid(pid);
    if (!proc)
    {
        return;
    }

    struct timeval tv;
    gettimeofday(&tv, nullptr);
    uint64_t now_us = static_cast<uint64_t>(tv.tv_sec) * 1000000ULL + tv.tv_usec;
    proc->last_exit_time_us = now_us;
    proc->pid = -1;

    int exit_code = 0;
    int term_sig  = 0;
    if (WIFEXITED(status))
    {
        exit_code = WEXITSTATUS(status);
        proc->last_exit_code   = exit_code;
        proc->last_exit_signal = 0;
    } else if (WIFSIGNALED(status))
    {
        term_sig = WTERMSIG(status);
        proc->last_exit_code   = 128 + term_sig;
        proc->last_exit_signal = term_sig;
    }

    bool crashed =
        (exit_code != 0) ||
        ((term_sig != 0) && (term_sig != SIGTERM) && (term_sig != SIGINT));

    wlr_log(WLR_INFO,
        "[process-supervisor] Service '%s' exited (exit_code=%d, sig=%d, "
        "crashed=%s, manually_stopped=%s, force_restart=%s)",
        proc->id.c_str(), exit_code, term_sig, (crashed ? "true" : "false"),
        (proc->manually_stopped ? "true" : "false"),
        (proc->force_restart ? "true" : "false"));

    if (proc->last_start_time_us > 0)
    {
        uint64_t uptime_ms = (now_us >= proc->last_start_time_us) ?
            ((now_us - proc->last_start_time_us) / 1000ULL) :
            0;
        if (uptime_ms >= 30000)
        {
            proc->restart_count = 0;
        }
    }

    if (proc->force_restart)
    {
        proc->force_restart = false;
        start_process(proc);
        return;
    }

    if (proc->manually_stopped)
    {
        proc->state = ProcessState::Stopped;
        notify_state_change(proc);
        return;
    }

    if (proc->type == StartupType::Restartable)
    {
        proc->state = ProcessState::Backoff;
        proc->restart_count++;

        uint32_t delay_ms = static_cast<uint32_t>(
            std::clamp(proc->restart_count * 1000, 1000, 10000));

        wlr_log(
            WLR_INFO,
            "[process-supervisor] Scheduling restart #%d for service '%s' in %u ms",
            proc->restart_count, proc->id.c_str(), delay_ms);

        notify_state_change(proc);

        if (proc->restart_timer)
        {
            wl_event_source_remove(proc->restart_timer);
        }

        if (event_loop_)
        {
            proc->restart_timer = wl_event_loop_add_timer(
                event_loop_, &ProcessSupervisor::handle_restart_timer, proc);
            wl_event_source_timer_update(proc->restart_timer, delay_ms);
        }
    } else
    {
        proc->state = crashed ? ProcessState::Crashed : ProcessState::Stopped;
        notify_state_change(proc);
    }
}

int ProcessSupervisor::handle_restart_timer(void *data)
{
    ManagedProcess *proc = static_cast<ManagedProcess*>(data);
    if (!proc || !proc->supervisor)
    {
        return 0;
    }

    if (proc->restart_timer)
    {
        wl_event_source_remove(proc->restart_timer);
        proc->restart_timer = nullptr;
    }

    if (!proc->manually_stopped)
    {
        proc->supervisor->start_process(proc);
    }

    return 0;
}

void ProcessSupervisor::notify_state_change(ManagedProcess *proc)
{
    if (!core_ || !core_->ipc_server || !proc)
    {
        return;
    }

    std::string json = serialize_process_json(proc);
    core_->ipc_server->broadcast_notification("onServiceStateChanged", json);
}

std::string ProcessSupervisor::serialize_process_json(const ManagedProcess *proc) const
{
    if (!proc)
    {
        return "{}";
    }

    struct timeval tv;
    gettimeofday(&tv, nullptr);
    uint64_t now_us = static_cast<uint64_t>(tv.tv_sec) * 1000000ULL + tv.tv_usec;

    uint64_t uptime_ms = 0;
    if ((proc->state == ProcessState::Running) &&
        (proc->last_start_time_us > 0) && (now_us >= proc->last_start_time_us))
    {
        uptime_ms = (now_us - proc->last_start_time_us) / 1000ULL;
    }

    const char *state_str = nullptr;
    switch (proc->state)
    {
      case ProcessState::Starting:
        state_str = "starting";
        break;

      case ProcessState::Running:
        state_str = "running";
        break;

      case ProcessState::Crashed:
        state_str = "crashed";
        break;

      case ProcessState::Backoff:
        state_str = "backoff";
        break;

      case ProcessState::Stopped:
        state_str = "stopped";
        break;
    }

    std::string s = "{";
    s += "\"id\":\"" + escape_json_str(proc->id.c_str()) + "\"";
    s += ",\"command\":\"" + escape_json_str(proc->command.c_str()) + "\"";
    s += ",\"type\":\"" +
        std::string(proc->type == StartupType::Restartable ? "restartable" :
        "normal") +
        "\"";
    s += ",\"state\":\"" + std::string(state_str) + "\"";
    s += ",\"pid\":" + std::to_string(proc->pid);
    s += ",\"restart_count\":" + std::to_string(proc->restart_count);
    s += ",\"last_exit_code\":" + std::to_string(proc->last_exit_code);
    s += ",\"last_exit_signal\":" + std::to_string(proc->last_exit_signal);
    s += ",\"uptime_ms\":" + std::to_string(uptime_ms);
    s += "}";
    return s;
}

std::string ProcessSupervisor::list_services_json() const
{
    std::string s = "[";
    bool first    = true;
    for (const auto & proc : processes_)
    {
        if (!first)
        {
            s += ',';
        }

        first = false;
        s    += serialize_process_json(proc.get());
    }

    s += ']';
    return s;
}

bool ProcessSupervisor::start_service_by_id(const std::string & id,
    std::string & out_json)
{
    ManagedProcess *proc = find_by_id(id);
    if (!proc)
    {
        return false;
    }

    bool ok = start_process(proc);
    out_json = serialize_process_json(proc);
    return ok;
}

bool ProcessSupervisor::stop_service_by_id(const std::string & id, int sig,
    std::string & out_json)
{
    ManagedProcess *proc = find_by_id(id);
    if (!proc)
    {
        return false;
    }

    bool ok = stop_process(proc, sig);
    out_json = serialize_process_json(proc);
    return ok;
}

bool ProcessSupervisor::restart_service_by_id(const std::string & id,
    std::string & out_json)
{
    ManagedProcess *proc = find_by_id(id);
    if (!proc)
    {
        return false;
    }

    bool ok = restart_process(proc);
    out_json = serialize_process_json(proc);
    return ok;
}
