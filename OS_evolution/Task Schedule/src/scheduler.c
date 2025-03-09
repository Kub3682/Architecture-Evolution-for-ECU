#include "scheduler.h"
#include <stdlib.h>
#include <sys/timerfd.h>
#include <stdio.h>
#include <signal.h>

#define MAX_NUM_CPU     256

#define SCHEDULER_PRIORITY 99

static char scheduler_stop = 0;

extern struct sched_class table_sched_class;

static struct scheduler *schedulers[MAX_NUM_CPU] = {NULL};
struct sched_class *sched_class_arr[NUM_SCHED_STRATEGY] = {&table_sched_class, NULL, NULL};

static inline void regist_scheduler(struct scheduler *scheduler, int cpu, enum schedule_strategy_t sched_strategy)
{
    schedulers[cpu] = scheduler;
    schedulers[cpu]->sched_class = sched_class_arr[sched_strategy];
}

static void kill_tasks(struct scheduler* sched)
{
    for(int i = 0; i < sched->task_num; i++) {
        pthread_kill(sched->tasks[i].tid, SIGINT);
    }
}

static void *scheduler_tick(void *s)
{
    struct scheduler *sched = (struct scheduler *)s;
    struct sched_class *sched_class = sched->sched_class;

    if(bind_cpu(sched->cpu) == RET_FAIL) {
        goto ret;
    }
    
    while(!scheduler_stop) {
        sched_class->schedule(sched);
    }

ret:
    kill_tasks(sched);
    return NULL;
}

int init_scheduler(struct scheduler *sched, struct sched_class *sched_class, int cpu, struct task_struct *tasks, int task_num)
{
    // 初始化成员变量
    INIT_LIST_HEAD(&sched->ready_queue);
    sched->tasks = tasks;
    sched->task_num = task_num;
    sched->cpu = cpu;
    sched->current_task = NULL;
    sched->sched_class = sched_class;

    // 初始化控制任务线程的条件变量
    pthread_mutex_init(&sched->mutex, NULL);
    pthread_cond_init(&sched->cond_var, NULL);

    // 创建调度器线程 TODO 设置pthread的亲、RT和优先级属性
    int ret = pthread_create(&sched->tid, NULL, scheduler_tick, (void *)sched);
    if(ret != 0) {
        printf("Create scheduler thread fail!\n");
        goto create_thread_fail;
    }

    struct sched_param scheduler_tick_param;
    scheduler_tick_param.sched_priority = SCHEDULER_PRIORITY;
    pthread_setschedparam(sched->tid, SCHED_FIFO, &scheduler_tick_param);

    return RET_SUCCESS;

create_thread_fail:
    pthread_mutex_destroy(&sched->mutex);
    pthread_cond_destroy(&sched->cond_var);

    return RET_FAIL;
}

void deinit_scheduler(struct scheduler *sched)
{
    pthread_join(sched->tid, NULL);
    pthread_mutex_destroy(&sched->mutex);
    pthread_cond_destroy(&sched->cond_var);
}

int create_scheduler(int cpu, enum schedule_strategy_t sched_strategy, struct task_struct *tasks, int task_num)
{
    if(cpu >= MAX_NUM_CPU || cpu < 0) {
        printf("The cpu is not within the managed range\n");
        return RET_FAIL;
    }
    
    if(sched_strategy >= NUM_SCHED_STRATEGY || sched_strategy < TABLE) {
        printf("Can not support the %d strategy\n", sched_strategy);
        return RET_FAIL;
    }

    struct sched_class *sched_class = sched_class_arr[sched_strategy];
    if(sched_class) {
        schedulers[cpu] = sched_class->scheduler_create(sched_class, cpu, tasks, task_num);
        if(!schedulers[cpu]) {
            printf("Create schedulers fail!\n");
            return RET_FAIL;
        }
    } else {
        printf("Can not support the %d strategy\n", sched_strategy);
        return RET_FAIL;
    }

    return RET_SUCCESS;
}

void destroy_scheduler(int cpu)
{
    if(schedulers[cpu]) {
        struct sched_class *sched_class = schedulers[cpu]->sched_class;
        sched_class->scheduler_destroy(schedulers[cpu]);
    }
}

void run_scheduler()
{
    for(int cpu = 0; cpu < MAX_NUM_CPU; cpu++) {
        if(schedulers[cpu]) {
            struct sched_class *sched_class = schedulers[cpu]->sched_class;
            sched_class->scheduler_start(schedulers[cpu]);
        }
    }
}

static void wait_wake_up(struct task_struct *task)
{
    pthread_mutex_lock(&task->sched->mutex);
        
    // 如果任务当前没有运行，则等待调度器唤醒
    while(task->task_state != RUNNING) {
        pthread_cond_wait(&task->sched->cond_var, &task->sched->mutex);
    }

    pthread_mutex_unlock(&task->sched->mutex);
}

static void terminate(struct task_struct *task)
{
    pthread_mutex_lock(&task->sched->mutex);
    task->task_state = SUSPENDED;
    pthread_cond_signal(&task->sched->cond_var);
    pthread_mutex_unlock(&task->sched->mutex);
    task->sched->sched_class->wake_up_scheduler(task->sched);
}

static void *periodic_task(void *args)
{
    struct task_struct *task = (struct task_struct *)args;
    bind_cpu(task->cpu);

    while(1) {
        wait_wake_up(task);
        task->task_function();
        terminate(task);
    }

    return NULL;
}

static void *single_shot_task(void *args)
{
    struct task_struct *task = (struct task_struct *)args;
    bind_cpu(task->cpu);
    wait_wake_up(task);
    task->task_function();
    terminate(task);

    return NULL;
}

struct task_struct *create_task_struct()
{
    struct task_struct *task = (struct task_struct *)malloc(sizeof(struct task_struct));
    // TODO 未来加入读取arxml创建任务
    return task;
}

void create_task(struct task_struct *task)
{   
    if(task) {
        switch(task->task_type) {
            case BASIC_PERIODIC:
                pthread_create(&task->tid, NULL, periodic_task, (void *)task);
                break;
            
            case BASIC_SINGLE_SHOT:
                pthread_create(&task->tid, NULL, single_shot_task, (void *)task);
                break;

            case EXTENDED_PERIODIC:
                // TODO
                break;

            case EXTENDED_SINGLE_SHOT:

                break;

            case ISR:

                break;

            default:

                break;
        }
        
    }
}

void destroy_task(struct task_struct *task)
{
    if(task) {
        pthread_join(task->tid, NULL);
        free(task);
    }
        
}


