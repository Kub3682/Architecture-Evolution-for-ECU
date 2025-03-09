#ifndef _TABLE_SCHED_H_
#define _TBALE_SCHED_H_

#include "scheduler.h"
#include <time.h>

// TODO 表项 任务 要统一 因为autosar其实是任务-表的映射，不是类型与表的映射

// 定义事件源，新事件在EVENT_NUM之前插入
#define FOREACH_EVENT_SRC(e) \
    e(EVENT_TIMER_TABLE_0) \
    e(EVENT_TIMER_TABLE_1) \
    e(EVENT_TASK) \
    e(EVENT_ISR)  \
    e(EVENT_NUM)

#define EVENT_TABLE_OFFSET 0
#define EVENT_TABLE_INDEX(x) ((x)-EVENT_TABLE_OFFSET)

enum event_src {
#define _EVENT(name) name,
    FOREACH_EVENT_SRC(_EVENT)
#undef _EVENT
};

enum table_type_t {
    PERIODIC,
    SINGLE_SHOT,
    NUM_TABLE_TYPE,
};

struct periodic_task_info {
    int period;
    int task_num;
    int event_task_num;
    struct task_struct **tasks;
    struct task_struct **event_tasks;
};

struct expiry_point {
    int offset;
    struct periodic_task_info *task_info;
};

struct schedule_table {
    int initial_offset;
    int duration;
    int final_delay;
    int num_eps;
    int current_ep_index;
    int num_task_infos;
    int merge_eps_num;
    int timerfd;
    enum table_type_t table_type;

#ifdef TEST
    int current_elapse;
    int elapse;
    struct timespec start;
#endif
    
    struct expiry_point *eps;
    struct periodic_task_info *task_infos;
};

struct table_scheduler {
    struct scheduler sched;
    int num_schedule_table;
    int epoll_fd;
    struct schedule_table **tables;
    int event_fds[EVENT_NUM];
};

#endif
