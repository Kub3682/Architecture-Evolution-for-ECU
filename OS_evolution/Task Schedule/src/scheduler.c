#include "scheduler.h"
#include <stdlib.h>
#include <sys/timerfd.h>
#include <stdio.h>
#include <signal.h>

#define SCHEDULER_PRIORITY 99
#define TASK_PRIORITY   50

static char scheduler_stop = 0;

extern struct sched_class table_sched_class;

//static struct scheduler *schedulers[MAX_NUM_CPU] = {NULL};
//static struct task_struct *tasks_persched[MAX_NUM_CPU] = {NULL};
//static int tasks_num_percpu[MAX_NUM_CPU] = {0};
struct sched_class *sched_class_arr[NUM_SCHED_STRATEGY] = {&table_sched_class, NULL, NULL};

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

static void inline wait_tasks_init(struct scheduler *sched)
{
    pthread_mutex_lock(&sched->mutex);
    while (sched->arrived_task_num < sched->task_num) {
        pthread_cond_wait(&sched->cond, &sched->mutex);
    }
    pthread_mutex_unlock(&sched->mutex);
}

void init_scheduler(struct scheduler *sched, struct sched_class *sched_class, int cpu, struct task_struct *tasks, int task_num)
{
    // 初始化成员变量
    INIT_LIST_HEAD(&sched->ready_queue);
    sched->tasks = tasks;
    sched->task_num = task_num;
    sched->arrived_task_num = 0;
    sched->cpu = cpu;
    sched->current_task = NULL;
    sched->sched_class = sched_class;
    pthread_mutex_init(&sched->mutex, NULL);
    pthread_cond_init(&sched->cond, NULL);

    for(int i = 0; i < sched->task_num; i++) {
        sched->tasks[i].sched = sched;
    }
}

int create_scheduler_threads(struct scheduler *sched)
{
    // 创建调度器线程 TODO 设置pthread的亲、RT和优先级属性
    pthread_attr_t attr;
    struct sched_param scheduler_param;

    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);

    scheduler_param.sched_priority = SCHEDULER_PRIORITY;  // 高于普通线程
    pthread_attr_setschedparam(&attr, &scheduler_param);

    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);

    int ret = pthread_create(&sched->tid, &attr, scheduler_tick, (void *)sched);
    if(ret != 0) {
        printf("Create scheduler thread fail!\n");
        return RET_FAIL;
    }

    create_tasks(sched->tasks, sched->task_num);
    wait_tasks_init(sched);

    return RET_SUCCESS;
}

void join_scheduler_threads(struct scheduler *sched)
{
    pthread_join(sched->tid, NULL);

    destroy_tasks(sched->tasks, sched->task_num);
}

void deinit_scheduler(struct scheduler *sched)
{
    pthread_mutex_destroy(&sched->mutex);
    pthread_cond_destroy(&sched->cond);
}

struct scheduler *create_scheduler(int cpu, enum schedule_strategy_t sched_strategy, struct task_struct *tasks, int task_num)
{
/*  已由调用者保证  
    if(cpu >= MAX_NUM_CPU || cpu < 0) {
        printf("The cpu is not within the managed range\n");
        return RET_FAIL;
    }
    
    if(sched_strategy >= NUM_SCHED_STRATEGY || sched_strategy < TABLE) {
        printf("Can not support the %d strategy\n", sched_strategy);
        return RET_FAIL;
    } */

    struct scheduler *sched = NULL;
    struct sched_class *sched_class = sched_class_arr[sched_strategy];
    if(sched_class) {
        sched = sched_class->scheduler_create(sched_class, cpu, tasks, task_num);
        if(!sched) {
            printf("Create schedulers fail!\n");
            return NULL;
        }
    } else {
        printf("Can not support the %d strategy\n", sched_strategy);
        return NULL;
    }

    return sched;
}

void destroy_scheduler(struct scheduler *scheduler)
{
    // 由调用者保证scheduler的合法性
    struct sched_class *sched_class = scheduler->sched_class;
    sched_class->scheduler_destroy(scheduler);
}

void run_scheduler(struct scheduler *scheduler)
{
    // 由调用者保证scheduler的合法性
    struct sched_class *sched_class = scheduler->sched_class;
    sched_class->scheduler_start(scheduler);
}

static void task_wait_resume(struct task_struct *task)
{
    pthread_mutex_lock(&task->mutex);

    if(task->task_state == INIT) {
        pthread_mutex_lock(&task->sched->mutex);
        task->sched->arrived_task_num++;
        task->task_state = SUSPENDED;
        if(task->sched->arrived_task_num == task->sched->task_num) {
            pthread_cond_signal(&task->sched->cond);
        }
        pthread_mutex_unlock(&task->sched->mutex);
    }
        
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

void load_tasks(struct task_struct_info *task_info_arr, int num, struct task_struct *tasks_persched[], int *tasks_num_percpu)
{
    if(!task_info_arr || num < 0) {
        printf("Load tasks info parameter error\n");
        exit(-1);
    }

    // 统计各CPU上tasks的数量，并创建对应的task结构
    for(int i = 0; i < num; i++) {
        int cpu = task_info_arr->cpu;
        if(cpu < MAX_NUM_CPU) {
            tasks_num_percpu[cpu]++;
        } else {
            printf("CPU %d is not online, task %p is abandoned\n", cpu, task_info_arr);
        }
    }

    for(int i = 0; i < MAX_NUM_CPU; i++) {
        if(tasks_num_percpu[i] > 0) {
            tasks_persched[i] = (struct task_struct *)malloc(sizeof(struct task_struct) * tasks_num_percpu[i]);
        }
    }

    // 载入task信息到task结构，并初始化task结构
    int tasks_index_percpu[MAX_NUM_CPU] = {0};
    
    for(int i = 0; i < num; i++) {
        int cpu = task_info_arr->cpu;
        int index = tasks_index_percpu[cpu]++;

        if(index >= tasks_num_percpu[cpu]) {
            printf("Load task info error, index %d out of bounds %d\n", index, tasks_num_percpu[cpu]);
            exit(-1);
        }

        struct task_struct *task = &tasks_persched[cpu][index];
        struct task_struct_info *info = &task_info_arr[i];

        task->priority = info->priority;
        task->cpu = info->cpu;
        task->period = info->period;
        task->is_preempted = 0;
        task->table_id = info->table_id;
        task->task_type = info->task_type;
        task->task_state = INIT;
        task->pfunc = info->pfunc;

        pthread_mutex_init(&task->mutex, NULL);
        pthread_cond_init(&task->cond_var, NULL);

        printf("task %p on CPU %d, period: %d, priority: %d, type: %d\n", task, cpu, task->period, task->priority, task->task_type);
    }

}

void create_tasks(struct task_struct *tasks, int num_tasks)
{
    pthread_attr_t attr;
    struct sched_param task_param;

    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);

    task_param.sched_priority = TASK_PRIORITY;  // 高于普通线程
    pthread_attr_setschedparam(&attr, &task_param);

    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);

    for(int i = 0; i < num_tasks; i++) {
        struct task_struct *task = &tasks[i];

        switch(task->task_type) {
            case BASIC_PERIODIC:
                pthread_create(&task->tid, &attr, periodic_task, (void *)task);
                break;
            
            case BASIC_SINGLE_SHOT:
                pthread_create(&task->tid, &attr, single_shot_task, (void *)task);
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
