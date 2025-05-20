#ifndef _SCHEDULER_H_
#define _SCHEDULER_H_

#define _GNU_SOURCE
#include "list.h"
#include "simulator.h"
#include <sched.h>
#include <pthread.h>
#include <stdio.h>
#include <ucontext.h>
#include <stdatomic.h>

#define RET_SUCCESS  0
#define RET_FAIL -1

#define FALSE 0
#define TRUE 1

#define MAX_NUM_CPU     256

#define SCHEDULE_MAX_PRIORITY   99
#define SCHEDULE_TIMING_PRIORITY 90
#define SCHEDULER_PRIORITY   50

#define STACK_SIZE 8192

#define MS_TO_NS(MS) ((MS) * 1000000)

enum task_state_t {
    TASK_INIT,
    TASK_SUSPENDED,
    TASK_READY,
    TASK_RUNNING,
};

enum sched_state_t {
    SCHED_INIT,
    SCHED_SUSPENDED,
    SCHED_RESUMING,
    SCHED_RUNNING,
    SCHED_WAITING,
};

struct scheduler;

struct task_struct {
    ucontext_t  ctx;
    int priority;
    int cpu;
    int period;
    int table_id;
    int in_ready_queue;
    enum task_type_t task_type;
    enum task_state_t task_state;
//    pthread_mutex_t task_mutex;
    struct list_head list;
    struct scheduler *sched;
    task_function pfunc;
    char *stack_ptr;
    int stack_size;
}__attribute__((aligned(64)));

struct scheduler {
    int cpu;
    int task_num;
    struct task_struct *current_task;
    struct sched_class	*sched_class;
    struct task_struct *tasks;

    pthread_t timing_tid;
    pthread_t scheduler_tid;

    ucontext_t  scheduler_ctx;
    _Atomic enum sched_state_t state;
    atomic_bool timing_arrived;
    
    struct list_head ready_queue;

    pthread_mutex_t scheduler_mutex;
    pthread_cond_t scheduler_init_cond;
    pthread_cond_t scheduler_resume_cond;
    sigset_t block_set;
    sigset_t prev_set;
}; 

struct sched_class {
    struct scheduler* (*scheduler_create)(struct sched_class *sched_class, int cpu, struct task_struct *tasks, int task_num);
    void (*scheduler_destroy)(struct scheduler *sched);
    void (*scheduler_start)(struct scheduler *sched);
    void (*enqueue)(struct scheduler *sched);
    void (*enqueue_task) (struct scheduler *sched, struct task_struct *task);
    struct task_struct *(*pick_next_task)(struct scheduler *sched);
    void (*task_fork)(struct task_struct *p);
    void (*task_dead)(struct task_struct *p);
    void (*wake_up_scheduler)(struct scheduler *sched);
    void (*schedule)(struct scheduler *sched);
    void (*yield)(struct scheduler *sched);
};

// scheduler functions
void load_tasks(struct task_struct_info *task_info_arr, int num, struct task_struct *tasks_persched[], int *tasks_num_percpu);
struct scheduler *create_scheduler(int cpu, enum schedule_strategy_t sched_strategy, struct task_struct *tasks, int task_num);
void destroy_scheduler(struct scheduler *scheduler);
void run_scheduler(struct scheduler *scheduler);
void init_scheduler(struct scheduler *sched, struct sched_class *sched_class, int cpu, struct task_struct *tasks, int task_num);
void deinit_scheduler(struct scheduler *sched);
int scheduler_create_threads(struct scheduler *sched);
void scheduler_join_threads(struct scheduler *sched);

void resume_scheduler_thread(struct scheduler *scheduler);
void preempt_task(struct scheduler *scheduler);

static inline void print_ready_queue(struct list_head *queue)
{
    struct task_struct *pos;

    list_for_each_entry(pos, queue, list) {
        printf("task: %p, period: %d, priority: %d\n", pos, pos->period, pos->priority);
    }
}

static inline void set_task_state(struct task_struct *task, enum task_state_t state)
{
//    phtread_mutex_lock(&task->mutex);
    task->task_state = state;
//    phtread_mutex_unlock(&task->mutex);
}

#endif