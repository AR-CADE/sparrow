#ifndef TASK_H
#define TASK_H

#include <sys/types.h>
#include <cstdint>
#include "flutter_embedder.h"

struct sparrow_render_task
{
    FlutterTask task;
    uint64_t target_time;
};

pid_t get_tid();
void sparrow_tasks_init();
void sparrow_tasks_finish();

#endif
