#include "util/realtime.hpp"
#include <cerrno>
#include <cstring>
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>
#include <sparrow/nonstd/wlroots-full.hpp>

#ifndef SCHED_RESET_ON_FORK
    #define SCHED_RESET_ON_FORK 0x40000000
#endif

bool sparrow_enable_realtime_scheduling(int priority)
{
    if (getenv("SPARROW_NO_REALTIME") != nullptr)
    {
        wlr_log(WLR_INFO, "[REALTIME] Real-time scheduling disabled by user flag (--no-realtime)");
        return false;
    }

    struct sched_param param = {};
    param.sched_priority = priority;

    // Try SCHED_RR with SCHED_RESET_ON_FORK
    int ret = sched_setscheduler(0, SCHED_RR | SCHED_RESET_ON_FORK, &param);
    if (ret == 0)
    {
        wlr_log(WLR_INFO,
            "[REALTIME] Soft-realtime scheduling enabled (SCHED_RR, priority=%d, RESET_ON_FORK)", priority);
        return true;
    }

    // Try standard SCHED_RR if SCHED_RESET_ON_FORK is not supported by kernel
    ret = sched_setscheduler(0, SCHED_RR, &param);
    if (ret == 0)
    {
        wlr_log(WLR_INFO, "[REALTIME] Soft-realtime scheduling enabled (SCHED_RR, priority=%d)", priority);
        return true;
    }

    // Try SCHED_FIFO as fallback
    ret = sched_setscheduler(0, SCHED_FIFO | SCHED_RESET_ON_FORK, &param);
    if (ret == 0)
    {
        wlr_log(WLR_INFO,
            "[REALTIME] Soft-realtime scheduling enabled (SCHED_FIFO, priority=%d, RESET_ON_FORK)", priority);
        return true;
    }

    // If unprivileged, try setting nice level to -10
    int nice_ret = setpriority(PRIO_PROCESS, 0, -10);
    if (nice_ret == 0)
    {
        wlr_log(WLR_INFO, "[REALTIME] High-priority nice level (-10) enabled as fallback");
        return true;
    }

    wlr_log(WLR_INFO,
        "[REALTIME] Real-time scheduling unavailable (errno=%d: %s), running with standard scheduler",
        errno, strerror(errno));
    return false;
}
