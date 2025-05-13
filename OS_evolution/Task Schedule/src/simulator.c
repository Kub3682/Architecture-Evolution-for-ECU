#include "simulator.h"
#include "scheduler.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

struct simulator {
    struct scheduler *schedulers[MAX_NUM_CPU];
    struct task_struct *tasks_persched[MAX_NUM_CPU];
    int tasks_num_percpu[MAX_NUM_CPU];
};

static struct simulator* instance = NULL;
static pthread_once_t init_once = PTHREAD_ONCE_INIT;

static inline void create_instance() 
{
    instance = (struct simulator *)malloc(sizeof(struct simulator));
    for(int i = 0; i < MAX_NUM_CPU; i++) {
        instance->schedulers[i] = NULL;
        instance->tasks_persched[i] = NULL;
        instance->tasks_num_percpu[i] = 0;
    }
}

static inline struct simulator* get_instance() 
{
    pthread_once(&init_once, create_instance);
    return instance;
}

void create_simulator(struct task_struct_info *task_info_arr, int num, enum schedule_strategy_t sched_strategy)
{
    // 创建simulator实体
    struct simulator *sim = get_instance();

    // 载入task信息
    load_tasks(task_info_arr, num, sim->tasks_persched, sim->tasks_num_percpu);

    // 创建各CPU上的调度器
    for(int cpu = 0; cpu < MAX_NUM_CPU; cpu++) {
        if(sim->tasks_num_percpu[cpu] > 0) {
            sim->schedulers[cpu] = create_scheduler(cpu, sched_strategy, sim->tasks_persched[cpu], sim->tasks_num_percpu[cpu]);
        }
    }
}

void run_simulator()
{
    struct simulator *simulator = get_instance(); 
    for(int cpu = 0; cpu < MAX_NUM_CPU; cpu++) {
        struct scheduler *scheduler = simulator->schedulers[cpu];
        if(scheduler) {
            run_scheduler(scheduler);
        }
    }
}

void destroy_simulator()
{
    struct simulator *simulator = get_instance(); 
    for(int cpu = 0; cpu < MAX_NUM_CPU; cpu++) {
        struct scheduler *scheduler = simulator->schedulers[cpu];
        if(scheduler) {
            destroy_scheduler(scheduler);
        }
    
        if(simulator->tasks_persched[cpu]) {
            free(simulator->tasks_persched[cpu]);
        }
    }

    free(simulator);
}