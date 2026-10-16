#include "util/dispatcher.hpp"
#include "core.hpp"

#include <sys/eventfd.h>
#include <unistd.h>
#include <queue>
#include <mutex>
#include <sparrow/nonstd/wlroots-full.hpp>

static int s_wayland_event_fd = -1;
static struct wl_event_source *s_event_source = nullptr;
static std::mutex s_task_queue_mutex;
static std::queue<std::function<void()>> s_task_queue;

static int handle_flutter_tasks(int fd, uint32_t mask, void *data)
{
    (void)mask;
    (void)data;
    uint64_t val;
    if (read(fd, &val, sizeof(val)) != sizeof(val))
    {
        return 1;
    }

    std::queue<std::function<void()>> local_queue;
    {
        std::lock_guard<std::mutex> lock(s_task_queue_mutex);
        std::swap(local_queue, s_task_queue);
    }

    while (!local_queue.empty())
    {
        if (local_queue.front())
        {
            local_queue.front()();
        }

        local_queue.pop();
    }

    return 1;
}

void sparrow_dispatcher_init(struct wl_display *display)
{
    if (s_wayland_event_fd >= 0)
    {
        return;
    }

    s_wayland_event_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (s_wayland_event_fd < 0)
    {
        wlr_log_errno(WLR_ERROR, "Failed to create eventfd for Wayland dispatcher");
        return;
    }

    struct wl_event_loop *loop = wl_display_get_event_loop(display);
    s_event_source = wl_event_loop_add_fd(loop, s_wayland_event_fd, WL_EVENT_READABLE,
        handle_flutter_tasks, nullptr);
    wlr_log(WLR_INFO, "[DISPATCHER] Thread-safe Wayland event loop dispatcher initialized (fd=%d)",
        s_wayland_event_fd);
}

void sparrow_dispatcher_finish()
{
    if (s_event_source)
    {
        wl_event_source_remove(s_event_source);
        s_event_source = nullptr;
    }

    if (s_wayland_event_fd >= 0)
    {
        close(s_wayland_event_fd);
        s_wayland_event_fd = -1;
    }
}

void sparrow_dispatch_to_wayland(std::function<void()> task)
{
    if ((s_wayland_event_fd < 0) || !task)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(s_task_queue_mutex);
        s_task_queue.push(std::move(task));
    }

    uint64_t val = 1;
    ssize_t res  = write(s_wayland_event_fd, &val, sizeof(val));
    (void)res;
}
