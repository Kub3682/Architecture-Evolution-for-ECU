#ifndef _TABLE_SCHED_H_
#define _TBALE_SCHED_H_

#include "scheduler.h"
#include <time.h>

// 定义本地事件源
#define FOREACH_NATIVE_EVENT_SRC(e) \
    e(NATIVE_EVENT_TASK) \
    e(NATIVE_EVENT_ISR)  \
    e(NATIVE_EVENT_NUM)

enum event_src {
#define _EVENT(name) name,
    FOREACH_NATIVE_EVENT_SRC(_EVENT)
#undef _EVENT
};

#define TABLE_SIZE 100

enum table_type_t {
    PERIODIC,
    SINGLE_SHOT,
    NUM_TABLE_TYPE,
};

struct task_info {
    struct hlist_node hlist;
    struct task_struct *task;
};

struct expiry_point {
    int offset;
    struct hlist_head tasks_hlist;
    struct hlist_head event_tasks_hlist;   
};

struct schedule_table {
    int initial_offset;
    int duration;
    int final_delay;
    int num_eps;
    int next_ep_index;
    int timerfd;
    enum table_type_t table_type;

#ifdef TEST
    int current_elapse;
    int elapse;
    struct timespec start;
#endif
    struct expiry_point *eps_array;
};

struct table_scheduler {
    struct scheduler sched;
    int num_schedule_table;
    int epoll_fd;
    int num_events;
    struct schedule_table **tables;
    int *event_fds;
    struct hlist_head event_hashtable[TABLE_SIZE];
};

struct event_func_args_t {
    struct table_scheduler *scheduler;
    int event_index;
    void *user_data;
};

typedef void (*process_event_func)(struct event_func_args_t *);

struct event_table_node {
    struct hlist_node hlist;
    process_event_func p_event_func;
    struct event_func_args_t args;
    int fd;
};

#endif
