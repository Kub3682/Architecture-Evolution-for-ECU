#ifndef _SCHEDULER_H_
#define _SCHEDULER_H_

#define _GNU_SOURCE
#include "list.h"
#include <sched.h>
#include <pthread.h>
#include <stdio.h>

#define RET_SUCCESS  0
#define RET_FAIL -1

struct sched_class;
struct scheduler;

enum task_type_t {
    BASIC_PERIODIC,
    BASIC_SINGLE_SHOT,
    EXTENDED_PERIODIC,
    EXTENDED_SINGLE_SHOT,
    ISR,
    UNKNOWN_TASK_TYPE,
};

enum task_state_t {
    SUSPENDED,
    READY,
    RUNNING,
    RESUMING,
    WAITING,
};

enum schedule_strategy_t {
    TABLE,
    FIFO,
    RR,
    NUM_SCHED_STRATEGY,
};

typedef void (*task_function)(void);

struct task_struct_info {
    int priority;
    int cpu;
    int period;
    int table_id;
    enum task_type_t task_type;
    task_function pfunc;
};

struct task_struct {
    pthread_t tid;
    int priority;
    int cpu;
    int period;
    int is_preempted;
    int table_id;
    enum task_type_t task_type;
    enum task_state_t task_state;
    enum task_state_t task_old_state;
    pthread_mutex_t mutex;
    pthread_cond_t cond_var;
    struct list_head list;
    struct scheduler *sched;
    task_function pfunc;
};

struct scheduler {
    int timer_fd;
    int cpu;
    pthread_t tid;
    int task_num;
    struct list_head ready_queue;
    struct task_struct *current_task;
    struct sched_class	*sched_class;
    struct task_struct *tasks;
//    pthread_mutex_t mutex;
//    pthread_cond_t cond_var;
}; 

struct sched_class {
    struct scheduler* (*scheduler_create)(struct sched_class *sched_class, int cpu, struct task_struct *tasks, int task_num);
    void (*scheduler_destroy)(struct scheduler *sched);
    void (*scheduler_start)(struct scheduler *sched);
    void (*enqueue_task) (struct scheduler *sched, struct task_struct *tasks, int num_tasks);
    struct task_struct *(*pick_next_task)(struct scheduler *sched);
    void (*task_fork)(struct task_struct *p);
    void (*task_dead)(struct task_struct *p);
    void (*wake_up_scheduler)(struct scheduler *sched);
    void (*schedule)(struct scheduler *sched);
};

static inline int bind_cpu(int cpu)
{   
    cpu_set_t cpuset;
    pthread_t tid = pthread_self();
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);

    if(pthread_setaffinity_np(tid, sizeof(cpuset), &cpuset) != 0) {
        printf("Thread %ld bind cpu %d error!\n", tid, cpu);
        return RET_FAIL;
    }

    return RET_SUCCESS;
}

int create_scheduler(int cpu, enum schedule_strategy_t sched_strategy, struct task_struct *task, int task_num);
void destroy_scheduler(int cpu);
void run_scheduler();

int init_scheduler(struct scheduler *sched, struct sched_class *sched_class, int cpu, struct task_struct *tasks, int task_num);
void deinit_scheduler(struct scheduler *sched);

struct task_struct *create_task(struct task_struct_info *info, task_function func);
void resume_task(struct task_struct *task);
void resume_preempted_task(struct task_struct *task);
void destroy_task(struct task_struct *task);
void create_tasks(struct task_struct *tasks, struct task_struct_info *task_struct_infos, int num_tasks);
void destroy_tasks(struct task_struct *tasks, int num_task);

#define MS_TO_NS(MS) ((MS) * 1000000)

#endif
