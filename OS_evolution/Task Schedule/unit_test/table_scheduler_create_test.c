#include "scheduler.h"
#include <stdlib.h>
#include <stdio.h>

int period_arr[] = {1,10,50,100,1000};
int period_arr_len = sizeof(period_arr)/sizeof(int);
int cpu_arr[] = {0};
int cpu_arr_len = sizeof(cpu_arr)/sizeof(int);

static void test_task()
{
    printf("Test_task\n");
}

// table_scheduler暂时要求table_id是连续的

static struct task_struct *user_create_tasks_random(int cpu, int num_task)
{
    struct task_struct *tasks = (struct task_struct *)malloc(sizeof(struct task_struct)*num_task);
    struct task_struct_info task_infos[num_task];

    int table_id = 0, max_table_id = num_task / 3;

    for(int i = 0; i < num_task; i++) {
        task_infos[i].priority = rand() % 10;   // 0-9
        task_infos[i].cpu = cpu;
        task_infos[i].period = period_arr[rand()%period_arr_len];
//        task_infos[i].table_id = (table_id++) % max_table_id;
        task_infos[i].table_id = 0;
        task_infos[i].task_type = BASIC_PERIODIC;
        task_infos[i].pfunc = test_task;
    }

    create_tasks(tasks, task_infos, num_task);

    return tasks;
}

static void user_destroy_tasks(struct task_struct *tasks, int num_task)
{
    if(tasks) {
        destroy_tasks(tasks, num_task);
        free(tasks);
    }
}

int main(int argc, char **argv)
{
    struct task_struct *tasks_percpu[cpu_arr_len];
    for(int i = 0; i < cpu_arr_len; i++) {
        tasks_percpu[i] = user_create_tasks_random(cpu_arr[i], 10);
    }

    sleep(10);

    for(int i = 0; i < cpu_arr_len; i++) {
        create_scheduler(cpu_arr[i], TABLE, tasks_percpu[i], 10);
    }

    run_scheduler();

    for(int i = 0; i < cpu_arr_len; i++) {
        destroy_scheduler(cpu_arr[i]);
    }

    for(int i = 0; i < cpu_arr_len; i++) {
        user_destroy_tasks(tasks_percpu[i], 10);
    }

    return 0;
}