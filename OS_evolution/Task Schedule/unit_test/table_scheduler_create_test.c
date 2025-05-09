#include "simulator.h"
#include <stdlib.h>
#include <stdio.h>

//int period_arr[] = {10,50,100,500,1000,10000};
int period_arr[] = {1};
int period_arr_len = sizeof(period_arr)/sizeof(int);
int cpu_arr[] = {0};
int cpu_arr_len = sizeof(cpu_arr)/sizeof(int);

static void test_task()
{
    printf("Test_task\n");
}

// table_scheduler暂时要求table_id是连续的

static struct task_struct_info *user_create_tasksinfo_random(int cpu, int num_task)
{
    struct task_struct_info *task_infos = (struct task_struct_info *)malloc(sizeof(struct task_struct_info)*num_task);

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

    return task_infos;
}

static void user_destroy_tasksinfo(struct task_struct_info *tasks_info)
{
    if(tasks_info) {
        free(tasks_info);
    }
}

int main(int argc, char **argv)
{
    int num_task = 1;
    int cpu = 0;
    struct task_struct_info *info_arr = user_create_tasksinfo_random(cpu, num_task);

    create_simulator(info_arr, num_task, TABLE);

    run_simulator();
    
    destroy_simulator();

    return 0;
}