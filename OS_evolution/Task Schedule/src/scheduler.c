#include "scheduler.h"
#include <stdlib.h>
#include <sys/timerfd.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

#define TASKS_THREAD_NUM    1

extern struct sched_class table_sched_class;
struct sched_class *sched_class_arr[NUM_SCHED_STRATEGY] = {&table_sched_class, NULL, NULL};

static char scheduler_stop = 0;
__thread struct scheduler *signal_sched = NULL;

static void disable_timing_preempt(struct scheduler *sched)
{
    struct sched_param param;
    param.sched_priority = SCHEDULE_MAX_PRIORITY;
    pthread_setschedparam(sched->scheduler_tid, SCHED_FIFO, &param);

    sigemptyset(&sched->block_set);
    sigaddset(&sched->block_set, SIGUSR1);  
    pthread_sigmask(SIG_BLOCK, &sched->block_set, &sched->prev_set);
}

static void enable_timing_preempt(struct scheduler *sched)
{
    struct sched_param param;
    param.sched_priority = SCHEDULER_PRIORITY;
    pthread_setschedparam(sched->scheduler_tid, SCHED_FIFO, &param);

    pthread_sigmask(SIG_SETMASK, &sched->prev_set, NULL);
}

static inline void entry(struct task_struct *task)
{
    atomic_store(&task->sched->state, SCHED_WAITING);
    task->task_state = TASK_RUNNING;
    enable_timing_preempt(task->sched);
}

static inline void terminate(struct task_struct *task)
{
    disable_timing_preempt(task->sched);
    task->task_state = TASK_SUSPENDED;
    atomic_store(&task->sched->state, SCHED_RUNNING);         // 下桩，确保在任务执行时（该桩前）可被抢占（即执行信号函数中的swapcontext）
    swapcontext(&task->ctx, &task->sched->scheduler_ctx);
}

static void periodic_task_entry(void *arg)
{
    struct task_struct *task = (struct task_struct *)arg;
    while(1) {
//        printf("task %p will excute, priority: %d, period: %d\n", task, task->priority, task->period);
        entry(task);
        task->pfunc();
        terminate(task);
    }
}

static void single_shot_task_entry(void *arg)
{
    struct task_struct *task = (struct task_struct *)arg;
    entry(task);
    task->pfunc();
    terminate(task);
}

/*static inline void yield(struct scheduler *sched)
{
    swapcontext(&sched->current_task.ctx, &main_ctx);
}*/

static inline void init_task_ctx(struct task_struct *task)
{
    getcontext(&task->ctx);
    task->ctx.uc_stack.ss_sp = task->stack_ptr;
    task->ctx.uc_stack.ss_size = task->stack_size;
    task->ctx.uc_link = &task->sched->scheduler_ctx;

    switch(task->task_type) {
        case BASIC_PERIODIC:
            makecontext(&task->ctx,  (void (*)(void))periodic_task_entry, 1, task);
            break;
        
        case BASIC_SINGLE_SHOT:
            makecontext(&task->ctx,  (void (*)(void))single_shot_task_entry, 1, task);
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

static void inline wait_scheduler_complete_init(struct scheduler *sched)
{
    pthread_mutex_lock(&sched->scheduler_mutex);
    while (atomic_load(&sched->state) == SCHED_INIT) {
        pthread_cond_wait(&sched->scheduler_init_cond, &sched->scheduler_mutex);
    }
    pthread_mutex_unlock(&sched->scheduler_mutex);
}

static void scheduler_wait_resume(struct scheduler *sched)
{
    pthread_mutex_lock(&sched->scheduler_mutex);

/*    if(atomic_load(&sched->state) == SCHED_INIT) {
        // sched->state = SUSPENDED;
        atomic_store(&sched->state, SCHED_SUSPENDED);*/
    if(atomic_exchange(&sched->state, SCHED_SUSPENDED) == SCHED_INIT) {
        pthread_cond_signal(&sched->scheduler_init_cond);
    }
        
    // 如果任务主线程当前没有运行，则等待调度器唤醒
    while(atomic_load(&sched->state) != SCHED_RESUMING) {
        pthread_cond_wait(&sched->scheduler_resume_cond, &sched->scheduler_mutex);
    }

    atomic_store(&sched->state, SCHED_RUNNING);

    pthread_mutex_unlock(&sched->scheduler_mutex);
}

static inline void switch_to(struct scheduler *sched)
{
    struct task_struct *current_task = sched->current_task;
//    current_task->task_state = TASK_RUNNING;
    swapcontext(&sched->scheduler_ctx, &current_task->ctx);
}

static void interrupt_task(int sig)
{
//    signal_sched->current_task->task_state = READY;
    if(atomic_load(&signal_sched->state) == SCHED_WAITING) {
        disable_timing_preempt(signal_sched);
        printf("Will interrupt task %p\n", signal_sched->current_task);
        atomic_store(&signal_sched->state, SCHED_RUNNING);
        swapcontext(&signal_sched->current_task->ctx, &signal_sched->scheduler_ctx);
    }
}

static void *scheduler_thread(void *args)
{
    struct scheduler *sched = (struct scheduler *)args;
    signal_sched = sched;

    bind_cpu(sched->cpu);

    /*初始化各任务的ctx*/
    getcontext(&sched->scheduler_ctx);
    for(int i = 0; i < sched->task_num; i++) {
        init_task_ctx(&sched->tasks[i]);
    }

    /*装载中断task的信号*/
    struct sigaction sa = {.sa_handler = interrupt_task, .sa_flags = SA_RESTART};
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);
    
    while(1) {
        scheduler_wait_resume(sched);

        struct timespec start, stop;
        clock_gettime(CLOCK_MONOTONIC, &start);
        printf("begin excuting task:\n");
        while(1) {
            disable_timing_preempt(sched);
            if(atomic_exchange(&sched->timing_arrived, FALSE)) {
                struct timespec enqueue_start, enqueue_stop;
                clock_gettime(CLOCK_MONOTONIC, &enqueue_start);
                sched->sched_class->enqueue(sched);
                if(sched->current_task && sched->current_task->task_state == TASK_RUNNING) {
                    sched->current_task->task_state = TASK_READY; 
                    sched->sched_class->enqueue_task(sched, sched->current_task);
                }
                clock_gettime(CLOCK_MONOTONIC, &enqueue_stop);
                long long elapse = enqueue_stop.tv_nsec - enqueue_start.tv_nsec + (enqueue_stop.tv_sec-enqueue_start.tv_sec) * 1000000000;
                printf("enqueue elapse: %lld ns\n", elapse);
//                print_ready_queue(&sched->ready_queue);
            }

            sched->current_task = sched->sched_class->pick_next_task(sched);
            if(sched->current_task) {
                printf("Select task: %p, period: %d, priority: %d\n", sched->current_task, sched->current_task->period, sched->current_task->priority);
//                print_ready_queue(&sched->ready_queue);                
                switch_to(sched);
                printf("Back to scheduler_thread!\n");
                enable_timing_preempt(sched);
            } else {
                atomic_store(&sched->state, SCHED_SUSPENDED);
                enable_timing_preempt(sched);
                printf("End current epoch schedule!\n");
                break;
            }
        }

        
        clock_gettime(CLOCK_MONOTONIC, &stop);
        long long elapse = stop.tv_nsec - start.tv_nsec + (stop.tv_sec-start.tv_sec) * 1000000000;
        printf("scheduler_thread elapse: %lld ns\n", elapse);
    }

    return NULL;
}

static void kill_scheduler(struct scheduler* sched)
{  
    pthread_kill(sched->scheduler_tid, SIGINT); 
}

static void *schedule_timing_thread(void *s)
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
    kill_scheduler(sched);
    return NULL;
}

static inline int create_scheduler_thread(struct scheduler *sched)
{
    pthread_attr_t attr;
    struct sched_param task_param;

    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);

    task_param.sched_priority = SCHEDULER_PRIORITY;  // 高于普通线程
    pthread_attr_setschedparam(&attr, &task_param);

    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);

    int ret = pthread_create(&sched->scheduler_tid, &attr, scheduler_thread, (void *)sched);
    if(ret != 0) {
        return RET_FAIL;
    }

    return RET_SUCCESS;
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
        task->table_id = info->table_id;
        task->in_ready_queue = FALSE;
        task->task_type = info->task_type;
        task->task_state = TASK_INIT;
        task->pfunc = info->pfunc;
        posix_memalign((void **)&task->stack_ptr, 16, STACK_SIZE);
        task->stack_size = STACK_SIZE;
        
        printf("task %p on CPU %d, period: %d, priority: %d, type: %d\n", task, cpu, task->period, task->priority, task->task_type);
    }

}

void init_scheduler(struct scheduler *sched, struct sched_class *sched_class, int cpu, struct task_struct *tasks, int task_num)
{
    // 初始化成员变量
    sched->state = ATOMIC_VAR_INIT(SCHED_INIT);
    sched->timing_arrived = ATOMIC_VAR_INIT(FALSE);
    INIT_LIST_HEAD(&sched->ready_queue);
    sched->tasks = tasks;
    sched->task_num = task_num;
    sched->cpu = cpu;
    sched->current_task = NULL;
    sched->sched_class = sched_class;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);

    pthread_mutex_init(&sched->scheduler_mutex, &attr);
    pthread_mutexattr_destroy(&attr);

    pthread_cond_init(&sched->scheduler_init_cond, NULL);
    pthread_cond_init(&sched->scheduler_resume_cond, NULL);

    for(int i = 0; i < sched->task_num; i++) {
        sched->tasks[i].sched = sched;
    }
}

void deinit_scheduler(struct scheduler *sched)
{
    pthread_mutex_destroy(&sched->scheduler_mutex);
    pthread_cond_destroy(&sched->scheduler_init_cond);
    pthread_cond_destroy(&sched->scheduler_resume_cond);
}

//创建调度时机捕获线程
int create_schedule_timing_thread(struct scheduler *sched)
{
    pthread_attr_t attr;
    struct sched_param scheduler_param;

    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);

    scheduler_param.sched_priority = SCHEDULE_TIMING_PRIORITY;  // 高于调度器线程，触发抢占
    pthread_attr_setschedparam(&attr, &scheduler_param);

    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);

    int ret = pthread_create(&sched->timing_tid, &attr, schedule_timing_thread, (void *)sched);
    if(ret != 0) {
        return RET_FAIL;
    }

    return RET_SUCCESS;
}

// 创建调度器中的线程
int scheduler_create_threads(struct scheduler *sched)
{
    int ret;
    ret = create_schedule_timing_thread(sched);
    if(ret != RET_SUCCESS) {
        printf("Create schedule timing thread fail!\n");
        exit(-1);
    }

    ret = create_scheduler_thread(sched);
    if(ret != RET_SUCCESS) {
        printf("Create scheduler thread fail!\n");
        exit(-1);
    }

    wait_scheduler_complete_init(sched);

    return RET_SUCCESS;
}

void scheduler_join_threads(struct scheduler *sched)
{
    pthread_join(sched->scheduler_tid, NULL);
    pthread_join(sched->timing_tid, NULL);
}

struct scheduler *create_scheduler(int cpu, enum schedule_strategy_t sched_strategy, struct task_struct *tasks, int task_num)
{
    // 由调用者保证参数的合法性
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

void resume_scheduler_thread(struct scheduler *scheduler)
{
    printf("Will resum scheduler thread: %ld\n", scheduler->scheduler_tid);
    pthread_mutex_lock(&scheduler->scheduler_mutex);
//    scheduler->state = RESUMING;
    atomic_store(&scheduler->state, SCHED_RESUMING);
    pthread_cond_signal(&scheduler->scheduler_resume_cond);
    pthread_mutex_unlock(&scheduler->scheduler_mutex);
}

void preempt_task(struct scheduler *scheduler)
{
    printf("Will preempt_task: %p\n", scheduler->current_task);
    pthread_kill(scheduler->scheduler_tid, SIGUSR1);
}
