#include "scheduler.h"
#include "table_sched.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <signal.h>

static void set_timer(int fd, long long ns) {
    struct itimerspec new_value;

    if(ns != 0) {
        new_value.it_value.tv_sec = 0;  
        new_value.it_value.tv_nsec = ns;
    } else {
        new_value.it_value.tv_sec = 0;  
        new_value.it_value.tv_nsec = 1;
    }
    
    new_value.it_interval.tv_sec = 0; 
    new_value.it_interval.tv_nsec = 0;

    timerfd_settime(fd, 0, &new_value, NULL);
}

static void table_scheuler_update_timer(struct schedule_table *table, const struct timespec *fix_start)
{
    int next_ep_index = table->next_ep_index;
    int current_ep_index = next_ep_index != 0 ? next_ep_index - 1 : table->num_eps - 1;
    int next_time;
    
    // new_ep_index < old_ep_index表明已经执行完一轮，如果是周期的就开启下一轮，如果是非周期表，就直接停止时钟
    if(table->table_type == PERIODIC) {
        next_time = next_ep_index > current_ep_index ? 
            table->eps[next_ep_index].offset - table->eps[current_ep_index].offset : table->final_delay + table->initial_offset;
    } else {
        next_time = next_ep_index > current_ep_index ? 
            table->eps[next_ep_index].offset - table->eps[current_ep_index].offset : 0;
    }

    table->merge_eps_num = 0;

    struct timespec fix_stop;
    clock_gettime(CLOCK_MONOTONIC, &fix_stop);
    long long fix_ns = MS_TO_NS(next_time) - (fix_stop.tv_nsec-fix_start->tv_nsec);

    fix_ns > 0 ? set_timer(table->timerfd, fix_ns) : set_timer(table->timerfd, 0);

#ifdef TEST
    clock_gettime(CLOCK_MONOTONIC, &table->start);
#endif
}

static void table_scheduler_start(struct scheduler *sched)
{
    struct table_scheduler *table_sched = container_of(sched, struct table_scheduler, sched);
    for(int i = 0; i < table_sched->num_schedule_table; i++) {
        set_timer(table_sched->tables[i]->timerfd, MS_TO_NS(table_sched->tables[i]->initial_offset));

#ifdef TEST
        table_sched->tables[i]->current_elapse = table_sched->tables[i]->initial_offset;
        clock_gettime(CLOCK_MONOTONIC, &table_sched->tables[i]->start);
#endif
    }
}

static inline void task_enqueue(struct list_head *queue, struct task_struct *task)
{
    struct task_struct *pos;

    list_for_each_entry(pos, queue, list) {
        if(task->priority > pos->priority) {
            // priority数值越大优先级越小，task插入到pos之后
            list_add(&task->list, &pos->list);
            return;
        }
    }

    // 没有在就绪队列中找到合适的位置（就绪队列中没有其他task），直接插入queue之后
    list_add(&task->list, queue);
}

static void expiry_point_enqueue(struct list_head *queue, struct expiry_point *ep)
{
    for(int i = 0; i < ep->task_info->task_num; i++) {
        struct task_struct *task = ep->task_info->tasks[i];
        task_enqueue(queue, task);
    }
}

static void table_scheduler_enqueue(struct scheduler *sched, struct schedule_table *table)
{
    int current_ep_index = table->next_ep_index;
    int ep_index = current_ep_index;
    while(table->eps[ep_index].offset == table->eps[current_ep_index].offset) {
        expiry_point_enqueue(&sched->ready_queue, &table->eps[ep_index]);
        table->merge_eps_num++;
        ep_index = (table->next_ep_index + 1) % table->num_eps;
    }
}

static struct task_struct *pick_next_task(struct scheduler *sched)
{
    struct list_head *head = &sched->ready_queue;
    struct task_struct *task = list_first_entry(head, struct task_struct, list);
    list_del(&task->list);
    return task;
}

static void process_tick(struct scheduler *sched, struct schedule_table *table, const struct timespec *fix_start)
{
    // 将到期点中的任务插入就绪队列
    table_scheduler_enqueue(sched, table);

    // 如果当前任务存在，中断当前任务，设置状态，然后插入就绪队列
    if(sched->current_task) {
        pthread_kill(sched->current_task->tid, SIGSTOP);
        sched->current_task->task_state = READY;
        task_enqueue(&sched->ready_queue, sched->current_task);
    }

    //  从就绪队列中选择新任务，设置对应任务，并唤醒该任务
    sched->current_task = pick_next_task(sched);
    pthread_kill(sched->current_task->tid, SIGCONT);
    sched->current_task->task_state = RUNNING;

    // 更新时钟
    table_scheuler_update_timer(table, fix_start);
}

static void table_scheduler_schedule(struct scheduler *sched)
{
    uint64_t expirations;
    struct table_scheduler *table_sched = container_of(sched, struct table_scheduler, sched);    

    struct epoll_event events[EVENT_NUM];
    printf("scheduler: %d\n", sched->cpu);
    int n = epoll_wait(table_sched->epoll_fd, events, EVENT_NUM, -1);
    
    struct timespec fix_start;
    clock_gettime(CLOCK_MONOTONIC, &fix_start);

#ifdef TEST
    struct timespec stop;
    clock_gettime(CLOCK_MONOTONIC, &stop);
    float elapse = (stop.tv_nsec - table->start.tv_nsec) / (float)1000000;
    if(old_ep_index > 0) {
        table->current_elapse = table->eps[old_ep_index].offset - table->eps[old_ep_index-1].offset;
        printf("Table theoretical elapse: %d, real elapse: %.2f\n", table->current_elapse, elapse);
    }
#endif

    for (int i = 0; i < n; i++) {
        if (events[i].data.fd == table_sched->event_fds[EVENT_TIMER_TABLE_0]) {
            // 处理Table 0 TICK到来调度
            process_tick(sched, table_sched->tables[EVENT_TABLE_INDEX(EVENT_TIMER_TABLE_0)], &fix_start);
        } else if(events[i].data.fd == table_sched->event_fds[EVENT_TIMER_TABLE_1]) {
            // 处理Table 1 TICK到来调度
            process_tick(sched, table_sched->tables[EVENT_TABLE_INDEX(EVENT_TIMER_TABLE_1)], &fix_start);
        } else if (events[i].data.fd == table_sched->event_fds[EVENT_TASK]) {
            // 处理任务结束引起的调度
        } else {
            // 处理中断
        }
    }
}

static int table_scheduler_create_epoll(struct table_scheduler *table_scheduler)
{
    // 创建epoll对象
    table_scheduler->epoll_fd = epoll_create1(0);
    if (table_scheduler->epoll_fd == -1) {
        printf("create epoll object fail\n");
        return -1;
    }

    // 创建event fd
    for(int i = 0; i < table_scheduler->num_schedule_table; i++) {
        // 创建table scheduler的timer
        struct schedule_table *table = table_scheduler->tables[i];
        table->timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
        if(table->timerfd == -1) {
            printf("Create scheduler timer fail!\n");
            goto create_timer_fail;
        }
        table_scheduler->event_fds[i] = table->timerfd;
    }
    
    for(int event_index = EVENT_TASK; event_index < EVENT_NUM; event_index++) {
        table_scheduler->event_fds[event_index] = eventfd(0, EFD_NONBLOCK);
        if(table_scheduler->event_fds[event_index] == -1) {
            printf("create fd of event %d fail\n", event_index);
            goto create_eventfd_fail;
        }
    }

    // 添加event到epoll中
    struct epoll_event event;
    for(int i = 0; i < table_scheduler->num_schedule_table; i++) {
        event.events = EPOLLIN;
        event.data.fd = table_scheduler->event_fds[i];
        if (epoll_ctl(table_scheduler->epoll_fd, EPOLL_CTL_ADD, table_scheduler->event_fds[i], &event) == -1) {
            printf("Add event %d fail\n", i);
            goto create_eventfd_fail;
        }
    }

    for(int event_index = EVENT_TASK; event_index < EVENT_NUM; event_index++) {
        event.events = EPOLLIN | EPOLLET;
        event.data.fd = table_scheduler->event_fds[event_index];
        if (epoll_ctl(table_scheduler->epoll_fd, EPOLL_CTL_ADD, table_scheduler->event_fds[event_index], &event) == -1) {
            printf("Add event %d fail\n", event_index);
            goto create_eventfd_fail;
        }
    }

    return RET_SUCCESS;

create_eventfd_fail:
    for(int event_index = 0; event_index < EVENT_NUM; event_index++) {
        if(table_scheduler->event_fds[event_index] != -1)
            close(table_scheduler->event_fds[event_index]);
    }

create_timer_fail:
    close(table_scheduler->epoll_fd);

    return RET_FAIL;
}

static void table_scheduler_destroy_epoll(struct table_scheduler *table_scheduler)
{
    for(int event_index = 0; event_index < EVENT_NUM; event_index++) {
        close(table_scheduler->event_fds[event_index]);
    }
    close(table_scheduler->epoll_fd);
}

#define GCD(a,b) ((a>=b)*GCD_1(a,b)+(a<b)*GCD_1(b,a))
#define GCD_1(a,b) ((((!(b)))*(a)) + (!!(b))*GCD_2((b), (a)%((b)+!(b))))
#define GCD_2(a,b) ((((!(b)))*(a)) + (!!(b))*GCD_3((b), (a)%((b)+!(b))))
#define GCD_3(a,b) ((((!(b)))*(a)) + (!!(b))*GCD_4((b), (a)%((b)+!(b))))
#define GCD_4(a,b) ((((!(b)))*(a)) + (!!(b))*GCD_5((b), (a)%((b)+!(b))))
#define GCD_5(a,b) ((((!(b)))*(a)) + (!!(b))*GCD_6((b), (a)%((b)+!(b))))
#define GCD_6(a,b) ((((!(b)))*(a)) + (!!(b))*GCD_7((b), (a)%((b)+!(b))))
#define GCD_7(a,b) ((((!(b)))*(a)) + (!!(b))*GCD_8((b), (a)%((b)+!(b))))
#define GCD_8(a,b) ((((!(b)))*(a)) + (!!(b))*GCD_last((b), (a)%((b)+!(b))))
#define GCD_last(a,b) (a)

#define LCM(a,b) (((a)*(b))/GCD(a,b))

//优先级按数值从小到大排序，数值越小优先级越高
static int compare_by_priority(const void *a, const void *b)
{
    struct task_struct **task_a = (struct task_struct **)a;
    struct task_struct **task_b = (struct task_struct **)b;
    
    return (*task_a)->priority - (*task_b)->priority;
}

static int compare_by_period(const void *a, const void *b) 
{
    int period_a = ((struct periodic_task_info *)a)->period;
    int period_b = ((struct periodic_task_info *)b)->period;

    if(period_a < period_b) return -1;
    if(period_a > period_b) return 1;
    return 0;
}

static void print_schedule_table(struct schedule_table *table)
{
    printf("Initial offset: %d, duration: %d, final_delay: %d, table_type: %d, num_eps: %d, num_task_infos: %d\n", 
                table->initial_offset, table->duration, table->final_delay, table->table_type, table->num_eps, table->num_task_infos);
    
    for(int i = 0; i < table->num_task_infos; i++) {
        for(int j = 0; j < table->task_infos[i].task_num; j++) {
            struct task_struct *task = table->task_infos[i].tasks[j];
            printf("Task type: %d, period: %d, priority: %d\n", task->task_type, task->period, task->priority);
        }

        for(int j = 0; j < table->task_infos[i].event_task_num; j++) {
            struct task_struct *task = table->task_infos[i].event_tasks[j];
            printf("Event_Task type: %d, period: %d, priority: %d\n", task->task_type, task->period, task->priority);
        }
    }  
}

static void print_periodic_task_info(struct periodic_task_info *info, int num_info)
{
    for(int i = 0; i < num_info; i++) {
        printf("periodic_task_info: %d, period: %d\n", i, info[i].period);
    }
}

static void table_scheduler_init(struct table_scheduler *table_scheduler, struct task_struct *tasks, const int task_num)
{
    int duration[NUM_TABLE_TYPE] = {1,1};
    int table_task_num[NUM_TABLE_TYPE] = {0,0};
    struct task_struct *table_task_array[NUM_TABLE_TYPE][task_num];
    
    // 确定每个调度表中任务数量和每个调度表的duration
    for(int i = 0; i < task_num; i++) {
        if(tasks[i].task_type == BASIC_PERIODIC || tasks[i].task_type == EXTENDED_PERIODIC) {
            table_task_array[PERIODIC][table_task_num[PERIODIC]] = &tasks[i];
            table_task_num[PERIODIC]++;
            duration[PERIODIC] = LCM(duration[PERIODIC], tasks[i].period);
        } else if(tasks[i].task_type == BASIC_SINGLE_SHOT || tasks[i].task_type == EXTENDED_SINGLE_SHOT) {
            table_task_array[SINGLE_SHOT][table_task_num[SINGLE_SHOT]] = &tasks[i];
            table_task_num[SINGLE_SHOT]++;
            duration[SINGLE_SHOT] = LCM(duration[SINGLE_SHOT], tasks[i].period);
        }
    }

//    printf("task_num: %d, PERIODIC task num: %d, SINGLE_SHOT task num: %d\n", task_num, table_task_num[PERIODIC], table_task_num[SINGLE_SHOT]);

    int num_table = 0;

    for(int i = 0; i < NUM_TABLE_TYPE; i++) {
        if(table_task_num[i] != 0) {
            num_table++;
        }
    }

    // 创建调度表存储空间
    table_scheduler->num_schedule_table = num_table;
    table_scheduler->tables = (struct schedule_table **)malloc(sizeof(struct schedule_table *)*num_table);
    for(int i = 0; i < num_table; i++) {
        table_scheduler->tables[i] = (struct schedule_table *)malloc(sizeof(struct schedule_table));
    }

    for(int i = 0; i < num_table; i++) {
        // 确定每个调度表中任务的周期信息
        int max_array_size = table_task_num[i];
        struct periodic_task_info *periodic_task_info = (struct periodic_task_info *)malloc(sizeof(struct periodic_task_info) * max_array_size);
        memset(periodic_task_info, 0, sizeof(struct periodic_task_info) * max_array_size);
        int num_periodic_info = 0;

        for(int j = 0; j < max_array_size; j++) {
            int k;
            // 查看当前任务周期是否在周期数组中
            for(k = 0; k < num_periodic_info; k++) {
                if(periodic_task_info[k].period != 0 && periodic_task_info[k].period == table_task_array[i][j]->period) {
                    break;
                }
            }

            // 添加新周期
            if(k == num_periodic_info) {
                periodic_task_info[num_periodic_info++].period = table_task_array[i][j]->period;
            }

            // 统计每个周期的任务数量
            if(table_task_array[i][j]->task_type == BASIC_PERIODIC || table_task_array[i][j]->task_type == BASIC_SINGLE_SHOT) {
                periodic_task_info[k].task_num++;  
            }

            if(table_task_array[i][j]->task_type == EXTENDED_PERIODIC || table_task_array[i][j]->task_type == EXTENDED_SINGLE_SHOT)
                periodic_task_info[k].event_task_num++;
        }

/*        for(int j = 0; j < num_periodic_info; j++) {
            printf("periodic_task_info[%d].task_num=%d, period: %d\n", j, periodic_task_info[j].task_num, periodic_task_info[j].period);
        } */

        // 为每个周期添加任务结构
        for(int j = 0; j < num_periodic_info; j++) {
            if(periodic_task_info[j].task_num > 0)
                periodic_task_info[j].tasks = (struct task_struct **)malloc(sizeof(struct task_struct *)*periodic_task_info[j].task_num);

            if(periodic_task_info[j].event_task_num > 0)
                periodic_task_info[j].event_tasks = (struct task_struct **)malloc(sizeof(struct task_struct *)*periodic_task_info[j].event_task_num);
            int task_index = 0;
            int event_task_index = 0;

            for(int k = 0; k < max_array_size; k++) {
                if(periodic_task_info[j].period == table_task_array[i][k]->period) {
                    if(table_task_array[i][k]->task_type == BASIC_PERIODIC || table_task_array[i][k]->task_type == BASIC_SINGLE_SHOT) {
                        periodic_task_info[j].tasks[task_index++] = table_task_array[i][k];
                    }

                    if(table_task_array[i][k]->task_type == EXTENDED_PERIODIC || table_task_array[i][k]->task_type == EXTENDED_SINGLE_SHOT) {
                        periodic_task_info[j].event_tasks[event_task_index++] = table_task_array[i][k];
                    }
                }
            }

/*            printf("task_index: %d, task_num: %d, event_task_index: %d, event_task_num: %d\n", task_index, periodic_task_info[j].task_num, 
                        event_task_index, periodic_task_info[j].event_task_num); */


            if(periodic_task_info[j].task_num > 0)
                qsort(periodic_task_info[j].tasks, periodic_task_info[j].task_num, sizeof(struct task_struct *), compare_by_priority);
            
            if(periodic_task_info[j].event_task_num > 0)
                qsort(periodic_task_info[j].event_tasks, periodic_task_info[j].event_task_num, sizeof(struct task_struct *), compare_by_priority);
        }

        qsort(periodic_task_info, num_periodic_info, sizeof(struct periodic_task_info), compare_by_period);

//        print_periodic_task_info(periodic_task_info, num_periodic_info);
        
        table_scheduler->tables[i]->num_task_infos = num_periodic_info;

        if(num_periodic_info > 0) {
            table_scheduler->tables[i]->task_infos = (struct periodic_task_info *)malloc(sizeof(struct periodic_task_info) * num_periodic_info);
        }

        memcpy(table_scheduler->tables[i]->task_infos, periodic_task_info, sizeof(struct periodic_task_info) * num_periodic_info);

        // 创建到期点，注意这里的到期点可能存在偏移量相同的情况，需要在更新时间时做合并
        int num_eps = 0;
        for(int j = 0; j < num_periodic_info; j++) {
            num_eps += duration[i] / periodic_task_info[j].period;
        }

        table_scheduler->tables[i]->num_eps = num_eps;
        table_scheduler->tables[i]->eps = (struct expiry_point *)malloc(sizeof(struct expiry_point)*num_eps);
        table_scheduler->tables[i]->next_ep_index = 0;
        int eps_index = 0;

        for(int time = 0; time < duration[i]; time++) {
            for(int j = 0; j < num_periodic_info; j++) {
                if(time % periodic_task_info[j].period == 0) {
                    table_scheduler->tables[i]->eps[eps_index].offset = time;
                    table_scheduler->tables[i]->eps[eps_index].task_info = &table_scheduler->tables[i]->task_infos[j];
                    eps_index++;
                }
            }
        }

        free(periodic_task_info);

        table_scheduler->tables[i]->initial_offset = table_scheduler->tables[i]->eps[0].offset;
        table_scheduler->tables[i]->duration = duration[i];
        table_scheduler->tables[i]->final_delay = duration[i] - table_scheduler->tables[i]->eps[table_scheduler->tables[i]->num_eps-1].offset;
        table_scheduler->tables[i]->merge_eps_num = 0;

        if(table_scheduler->tables[i]->task_infos[0].tasks[0]->task_type == BASIC_PERIODIC || 
                table_scheduler->tables[i]->task_infos[0].tasks[0]->task_type == EXTENDED_PERIODIC) {
            table_scheduler->tables[i]->table_type = PERIODIC;
        }

        if(table_scheduler->tables[i]->task_infos[0].tasks[0]->task_type == BASIC_SINGLE_SHOT || 
                table_scheduler->tables[i]->task_infos[0].tasks[0]->task_type == EXTENDED_SINGLE_SHOT) {
            table_scheduler->tables[i]->table_type = SINGLE_SHOT;
        }

        print_schedule_table(table_scheduler->tables[i]);
    }
}

static void table_scheduler_deinit(struct table_scheduler *table_scheduler)
{
    for(int i = 0; i < table_scheduler->num_schedule_table; i++) {
        for(int j = 0; j < table_scheduler->tables[i]->num_task_infos; j++) {
            if(table_scheduler->tables[i]->task_infos[j].task_num > 0)
                free(table_scheduler->tables[i]->task_infos[j].tasks);

            if(table_scheduler->tables[i]->task_infos[j].event_task_num > 0)
                free(table_scheduler->tables[i]->task_infos[j].event_tasks);
        }

        free(table_scheduler->tables[i]->task_infos);
        free(table_scheduler->tables[i]->eps);
        free(table_scheduler->tables[i]);
    }
    free(table_scheduler->tables);
}

static struct scheduler *table_scheduler_create(struct sched_class *sched_class, int cpu, struct task_struct *tasks, int task_num)
{
    struct table_scheduler *table_scheduler = (struct table_scheduler *)malloc(sizeof(struct table_scheduler));
    struct scheduler *sched = &table_scheduler->sched;
    
    // 根据任务初始化调度表
    table_scheduler_init(table_scheduler, tasks, task_num);
    
    // 创建调度器的epoll
    int ret = table_scheduler_create_epoll(table_scheduler);
    if(ret == -1) {
        goto create_epoll_fail;
    }

    // 初始化调度器（通用）
    if(init_scheduler(sched, sched_class, cpu, tasks, task_num) == RET_FAIL) {
        printf("Init scheduler fail!\n");
        goto init_scheduler_fail;
    }
    
    return (struct scheduler *)table_scheduler;

    //失败后的资源销毁
init_scheduler_fail:
    table_scheduler_destroy_epoll(table_scheduler);

create_epoll_fail:
    table_scheduler_deinit(table_scheduler);
    free(table_scheduler);
    return NULL;
}

static void table_scheduler_destroy(struct scheduler *sched)
{
    struct table_scheduler *table_scheduler = container_of(sched, struct table_scheduler, sched);

    deinit_scheduler(sched);

    table_scheduler_destroy_epoll(table_scheduler);
    
    table_scheduler_deinit(table_scheduler);

    free(table_scheduler);
}

//由Task结束时调用，唤醒调度器
static void table_scheduler_wake_up(struct scheduler *sched)
{
    struct table_scheduler *table_sched = container_of(sched, struct table_scheduler, sched);
    uint64_t u = 1;
    ssize_t s = write(table_sched->event_fds[EVENT_TASK], &u, sizeof(uint64_t));
    if (s != sizeof(uint64_t)) {
        perror("write");
    }
}

struct sched_class table_sched_class = {
    .scheduler_create = table_scheduler_create,
    .scheduler_destroy = table_scheduler_destroy,
    .scheduler_start = table_scheduler_start,
    .schedule = table_scheduler_schedule,
};

