#pragma once

#include <cstdint>

/**
 * Enable soft-realtime scheduling (SCHED_RR or SCHED_FIFO) for the current thread.
 *
 * Uses SCHED_RESET_ON_FORK so that all child processes spawned by Sparrow (such as desktop applications,
 * games, terminals) automatically revert to standard SCHED_OTHER and do not inherit realtime priority.
 *
 * @param priority Realtime priority (default 20, range 1-99)
 * @return true if realtime scheduling was successfully applied, false otherwise.
 */
bool sparrow_enable_realtime_scheduling(int priority = 20);
