#include "scheduler.h"
#include <stdlib.h>
#include <stdio.h>

int period_arr[] = {1,10,50,100,1000};
int period_arr_len = sizeof(period_arr)/sizeof(int);
int cpu_arr[] = {0, 2, 4, 6};
int cpu_arr_len = sizeof(cpu_arr)/sizeof(int);

static void test_task()
{
    printf("Test_task");
}

static struct task_struct *create_tasks_random(int cpu, int num_task)
{
    struct task_struct *tasks = (struct task_struct *)malloc(sizeof(struct task_struct)*num_task);

    for(int i = 0; i < num_task; i++) {
        tasks[i].priority = rand() % 10;   // 0-9
        tasks[i].cpu = cpu;
        tasks[i].period = period_arr[rand()%period_arr_len];
        tasks[i].is_preempted = 0;
        tasks[i].task_type = rand()%2;
        tasks[i].task_state = SUSPENDED;
        tasks[i].task_function = test_task;
    }

    return tasks;
}

static void destroy_tasks(struct task_struct *tasks)
{
    if(tasks) {
        free(tasks);
    }
}

int main(int argc, char **argv)
{
    struct task_struct *tasks_percpu[cpu_arr_len];
    for(int i = 0; i < cpu_arr_len; i++) {
        tasks_percpu[i] = create_tasks_random(cpu_arr[i], 10);
    }

    for(int i = 0; i < cpu_arr_len; i++) {
        create_scheduler(cpu_arr[i], TABLE, tasks_percpu[i], 10);
    }

    run_scheduler();

    for(int i = 0; i < cpu_arr_len; i++) {
        destroy_scheduler(cpu_arr[i]);
    }

    for(int i = 0; i < cpu_arr_len; i++) {
        destroy_tasks(tasks_percpu[i]);
    }

    return 0;
}