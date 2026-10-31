#ifndef PROCESS_SUPERVISOR_HPP
#define PROCESS_SUPERVISOR_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Core;
struct wl_event_loop;
struct wl_event_source;

enum class StartupType
{
    Normal,
    Restartable,
};

enum class ProcessState
{
    Stopped,
    Starting,
    Running,
    Crashed,
    Backoff,
};

struct ManagedProcess
{
    std::string id;
    std::string command;
    StartupType type   = StartupType::Normal;
    ProcessState state = ProcessState::Stopped;
    pid_t pid = -1;
    int restart_count    = 0;
    int last_exit_code   = 0;
    int last_exit_signal = 0;
    uint64_t last_start_time_us = 0;
    uint64_t last_exit_time_us  = 0;
    bool manually_stopped = false;
    bool force_restart    = false;
    struct wl_event_source *restart_timer = nullptr;
    // Child exit notification: pidfd watched by the wl_event_loop (preferred),
    // or a polling timer when pidfd_open() is unavailable (kernel < 5.3).
    int pidfd = -1;
    struct wl_event_source *exit_source = nullptr;
    class ProcessSupervisor *supervisor = nullptr;
};

class ProcessSupervisor
{
  public:
    explicit ProcessSupervisor(Core *core);
    ~ProcessSupervisor();

    bool init(struct wl_event_loop *loop);
    void shutdown();

    void start_all();

    bool start_process(ManagedProcess *proc);
    bool stop_process(ManagedProcess *proc, int sig = 15);
    bool restart_process(ManagedProcess *proc);

    ManagedProcess *find_by_id(const std::string & id);
    ManagedProcess *find_by_pid(pid_t pid);

    ManagedProcess *add_or_update_service(const std::string & id,
        const std::string & command,
        StartupType type);

    std::string list_services_json() const;
    bool start_service_by_id(const std::string & id, std::string & out_json);
    bool stop_service_by_id(const std::string & id, int sig, std::string & out_json);
    bool restart_service_by_id(const std::string & id, std::string & out_json);

    std::string serialize_process_json(const ManagedProcess *proc) const;

  private:
    static int handle_pidfd(int fd, uint32_t mask, void *data);
    static int handle_exit_poll(void *data);
    static int handle_restart_timer(void *data);

    void watch_child(ManagedProcess *proc);
    void unwatch_child(ManagedProcess *proc);
    bool reap_child(ManagedProcess *proc);
    void on_child_exit(pid_t pid, int status);
    void notify_state_change(ManagedProcess *proc);

    Core *core_ = nullptr;
    struct wl_event_loop *event_loop_ = nullptr;
    std::vector<std::unique_ptr<ManagedProcess>> processes_;
    bool started_all_ = false;
};

#endif // PROCESS_SUPERVISOR_HPP
