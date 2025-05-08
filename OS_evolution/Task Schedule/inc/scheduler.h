#ifndef _SCHEDULER_H_
#define _SCHEDULER_H_

#define _GNU_SOURCE
#include "list.h"
#include "simulator.h"
#include <sched.h>
#include <pthread.h>
#include <stdio.h>

#define RET_SUCCESS  0
#define RET_FAIL -1

#define MAX_NUM_CPU     256

struct sched_class;
struct scheduler;

enum task_state_t {
    INIT,
    SUSPENDED,
    READY,
    RUNNING,
    RESUMING,
    WAITING,
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
    int cpu;
    pthread_t tid;
    int task_num;
    int arrived_task_num;
    struct list_head ready_queue;
    struct task_struct *current_task;
    struct sched_class	*sched_class;
    struct task_struct *tasks;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
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

struct scheduler *create_scheduler(int cpu, enum schedule_strategy_t sched_strategy, struct task_struct *tasks, int task_num);
void destroy_scheduler(struct scheduler *scheduler);
void run_scheduler(struct scheduler *scheduler);

void init_scheduler(struct scheduler *sched, struct sched_class *sched_class, int cpu, struct task_struct *tasks, int task_num);
int create_scheduler_threads(struct scheduler *sched);
void join_scheduler_threads(struct scheduler *sched);
void deinit_scheduler(struct scheduler *sched);

void load_tasks(struct task_struct_info *task_info_arr, int num, struct task_struct *tasks_persched[], int *tasks_num_percpu);
void resume_task(struct task_struct *task);
void resume_preempted_task(struct task_struct *task);
void create_tasks(struct task_struct *tasks, int num_tasks);
void destroy_tasks(struct task_struct *tasks, int num_task);

#define MS_TO_NS(MS) ((MS) * 1000000)

#endif
