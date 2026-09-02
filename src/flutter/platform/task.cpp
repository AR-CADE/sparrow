#include <sys/syscall.h>
#include <unistd.h>
#include <vector>
#include <mutex>

#include "core.hpp"
#include "task.hpp"

pid_t get_tid()
{
#ifdef SYS_gettid
    return syscall(SYS_gettid);
#else
    #error "SYS_gettid unavailable on this system"
#endif
}

// Must be called with a lock on instance->platform_task_mutex
static void engine_platform_task_queue_schedule()
{
    Core *instance = Core::instance();

    uint64_t first_task_ns = UINT64_MAX;
    for (const auto & t : instance->queued_platform_tasks)
    {
        if (first_task_ns > t.target_time)
        {
            first_task_ns = t.target_time;
        }
    }

    if (first_task_ns != UINT64_MAX)
    {
        const uint64_t current_time_nanos = instance->embedder_api.GetCurrentTime();
        if (first_task_ns <= current_time_nanos)
        {
            // Run right away
            eventfd_write(instance->platform_notify_fd, 1);
            // Disable timer
            wl_event_source_timer_update(instance->platform_timer_event_source, 0);
        } else
        {
            const int64_t diff_ns = first_task_ns - current_time_nanos;
            const int64_t diff_ms = diff_ns / 1000000;

            // Enable timer
            wl_event_source_timer_update(instance->platform_timer_event_source,
                diff_ms + 1);
        }
    } else
    {
        // Disable timer
        wl_event_source_timer_update(instance->platform_timer_event_source, 0);
    }
}

void engine_platform_task_queue_process(void *user_data)
{
    auto *instance = static_cast<Core*>(user_data);
    const uint64_t current_time_nanos = instance->embedder_api.GetCurrentTime();

    std::vector<sparrow_render_task> ready_tasks;
    {
        std::scoped_lock lock(instance->platform_task_mutex);
        if (instance->queued_platform_tasks.empty())
        {
            return;
        }

        std::vector<sparrow_render_task> remaining_tasks;
        remaining_tasks.reserve(instance->queued_platform_tasks.size());
        ready_tasks.reserve(instance->queued_platform_tasks.size());

        for (const auto & item : instance->queued_platform_tasks)
        {
            if (current_time_nanos >= item.target_time)
            {
                ready_tasks.push_back(item);
            } else
            {
                remaining_tasks.push_back(item);
            }
        }

        instance->queued_platform_tasks = std::move(remaining_tasks);
        engine_platform_task_queue_schedule();
    }

    // Execute ready tasks outside the lock
    for (const auto & item : ready_tasks)
    {
        if (instance->embedder_api.RunTask(instance->engine, &item.task) !=
            kSuccess)
        {
            wlr_log(WLR_ERROR, "Failed to run render task?!");
        }
    }
}

// Callback from the event loop, on task timer expiry.
static int engine_platform_task_queue_timer(void *user_data)
{
    engine_platform_task_queue_process(user_data);
    return 0;
}

// Callback from event loop, on notifyfd.
static int engine_renderer_task_queue_notifyfd(int fd, uint32_t mask,
    void *user_data)
{
    (void)fd;
    (void)mask;
    auto *instance = static_cast<Core*>(user_data);

    // Reset eventfd
    eventfd_t value;
    eventfd_read(instance->platform_notify_fd, &value);

    // Process tasks.
    engine_platform_task_queue_process(user_data);

    // Schedule frames on main thread for outputs that received damage
    Output *output = nullptr;
    wl_list_for_each(output, &instance->outputs, link)
    {
        if (output && output->wlr_output && output->wlr_output->enabled)
        {
            wlr_output_schedule_frame(output->wlr_output);
        }
    }

    return 0;
}

static bool engine_cb_platform_runs_on_current_thread(void *user_data)
{
    auto *instance = static_cast<Core*>(user_data);
    return get_tid() == instance->platform_tid;
}

static void engine_cb_platform_post_task(FlutterTask task, uint64_t target_time,
    void *user_data)
{
    auto *instance = static_cast<Core*>(user_data);

    {
        std::scoped_lock lock(instance->platform_task_mutex);
        instance->queued_platform_tasks.push_back(sparrow_render_task{
            .task = task,
            .target_time = target_time,
        });
        engine_platform_task_queue_schedule();
    }
}

void sparrow_tasks_init()
{
    Core *instance = Core::instance();

    instance->platform_tid = get_tid();

    instance->platform_notify_fd = eventfd(0, 0);
    instance->platform_notify_event_source = wl_event_loop_add_fd(
        instance->wl_event_loop, instance->platform_notify_fd, WL_EVENT_READABLE,
        engine_renderer_task_queue_notifyfd, instance);

    instance->platform_timer_event_source = wl_event_loop_add_timer(
        instance->wl_event_loop, engine_platform_task_queue_timer, instance);

    instance->platform_task_runner.struct_size =
        sizeof(FlutterTaskRunnerDescription);
    instance->platform_task_runner.identifier = 0;
    instance->platform_task_runner.user_data  = instance;
    instance->platform_task_runner.runs_task_on_current_thread_callback =
        engine_cb_platform_runs_on_current_thread;
    instance->platform_task_runner.post_task_callback =
        engine_cb_platform_post_task;

    instance->custom_task_runners.struct_size = sizeof(FlutterCustomTaskRunners);
    instance->custom_task_runners.platform_task_runner =
        &instance->platform_task_runner;
}

void sparrow_tasks_finish()
{
    Core *instance = Core::instance();
    if (instance->platform_notify_event_source != nullptr)
    {
        wl_event_source_remove(instance->platform_notify_event_source);
        instance->platform_notify_event_source = nullptr;
    }

    if (instance->platform_timer_event_source != nullptr)
    {
        wl_event_source_remove(instance->platform_timer_event_source);
        instance->platform_timer_event_source = nullptr;
    }

    if (instance->platform_notify_fd >= 0)
    {
        close(instance->platform_notify_fd);
        instance->platform_notify_fd = -1;
    }
}
