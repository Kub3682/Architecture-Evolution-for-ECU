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

    for(int i = 0; i < task_num; i++) {
        tasks[i].sched = sched;
    }

    // 创建调度器线程 TODO 设置pthread的亲、RT和优先级属性
    int ret = pthread_create(&sched->tid, NULL, scheduler_tick, (void *)sched);
    if(ret != 0) {
        printf("Create scheduler thread fail!\n");
        return RET_FAIL;
    }

    struct sched_param scheduler_tick_param;
    scheduler_tick_param.sched_priority = SCHEDULER_PRIORITY;
    pthread_setschedparam(sched->tid, SCHED_FIFO, &scheduler_tick_param);

    return RET_SUCCESS;
}

void deinit_scheduler(struct scheduler *sched)
{
    pthread_join(sched->tid, NULL);
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

static void task_wait_resume(struct task_struct *task)
{
    pthread_mutex_lock(&task->mutex);
        
    // 如果任务当前没有运行，则等待调度器唤醒
    while(task->task_state != RESUMING) {
        pthread_cond_wait(&task->cond_var, &task->mutex);
    }

    task->task_state = RUNNING;

    pthread_mutex_unlock(&task->mutex);
}

static void terminate(struct task_struct *task)
{
    pthread_mutex_lock(&task->mutex);
    task->task_state = SUSPENDED;
    pthread_cond_signal(&task->cond_var);
    pthread_mutex_unlock(&task->mutex);
    printf("Task: %p will wakeup scheduler\n", task);
    task->sched->sched_class->wake_up_scheduler(task->sched);
}

static void *periodic_task(void *args)
{
    struct task_struct *task = (struct task_struct *)args;
    bind_cpu(task->cpu);

    while(1) {
        task_wait_resume(task);
        printf("task %p will executing\n", task);
        task->pfunc();
        terminate(task);
    }

    return NULL;
}

static void *single_shot_task(void *args)
{
    struct task_struct *task = (struct task_struct *)args;
    bind_cpu(task->cpu);

    task_wait_resume(task);
    task->pfunc();
    terminate(task);

    return NULL;
}

struct task_struct *create_task_struct()
{
    struct task_struct *task = (struct task_struct *)malloc(sizeof(struct task_struct));
    // TODO 未来加入读取arxml创建任务
    return task;
}

struct task_struct *create_task(struct task_struct_info *info, task_function func)
{
    struct task_struct *task = (struct task_struct *)malloc(sizeof(struct task_struct));
    if(task) {
        task->priority = info->priority;
        task->cpu = info->cpu;
        task->period = info->period;
        task->is_preempted = 0;
        task->table_id = info->table_id;
        task->task_type = info->task_type;
        task->task_state = SUSPENDED;
        task->pfunc = func;

        pthread_mutex_init(&task->mutex, NULL);
        pthread_cond_init(&task->cond_var, NULL);

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

    return task;
}

void destroy_task(struct task_struct *task)
{
    if(task) {
        pthread_join(task->tid, NULL);
        pthread_mutex_destroy(&task->mutex);
        pthread_cond_destroy(&task->cond_var);
        free(task);
    }      
}

void create_tasks(struct task_struct *tasks, struct task_struct_info *task_struct_infos, int num_tasks)
{
    for(int i = 0; i < num_tasks; i++) {
        struct task_struct *task = &tasks[i];
        struct task_struct_info *info = &task_struct_infos[i];
        task->priority = info->priority;
        task->cpu = info->cpu;
        task->period = info->period;
        task->is_preempted = 0;
        task->table_id = info->table_id;
        task->task_type = info->task_type;
        task->task_state = SUSPENDED;
        task->pfunc = info->pfunc;

        pthread_mutex_init(&task->mutex, NULL);
        pthread_cond_init(&task->cond_var, NULL);
        printf("task[%d]:%p, period: %d, priority: %d, type: %d\n", i, task, task->period, task->priority, task->task_type);

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

void destroy_tasks(struct task_struct *tasks, int num_task)
{
    for(int i = 0; i < num_task; i++) {
        struct task_struct *task = &tasks[i];
        pthread_join(task->tid, NULL);
        pthread_mutex_destroy(&task->mutex);
        pthread_cond_destroy(&task->cond_var);   
    }
}

void resume_task(struct task_struct *task)
{
    if(task) {
        printf("Will resum task: %p\n", task);
        pthread_mutex_lock(&task->mutex);
        pthread_cond_signal(&task->cond_var);
        pthread_mutex_unlock(&task->mutex);
    }
}

void resume_preempted_task(struct task_struct *task)
{
    if(task) {
        task->task_state = task->task_old_state;
        printf("Will resum preempted task: %p, state: %d\n", task, task->task_state);

        if(task->task_state == RESUMING && pthread_mutex_trylock(&task->mutex) == 0) {
            // 表明任务在收到信号前就被抢占，需要再发一次信号
            pthread_cond_signal(&task->cond_var);
            pthread_mutex_unlock(&task->mutex);
            printf("aaa\n");
        }

        if(task->task_state == SUSPENDED && pthread_mutex_trylock(&task->mutex) == 0) {
            pthread_mutex_unlock(&task->mutex);
            task->sched->sched_class->wake_up_scheduler(task->sched);
            printf("bbb\n");
        }
            
        // 退出，交由OS调度器来恢复任务 
    }
}
