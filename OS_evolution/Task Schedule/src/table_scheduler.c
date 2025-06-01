#include "scheduler.h"
#include "table_sched.h"
#include "list.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <errno.h>

#define INVALID_OFFSET     -1

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

struct timespec process_start = {0,0}, process_stop;
long long process_elapse;

static inline void task_enqueue_check(struct list_head *queue, struct task_struct *new_task)
{
    struct task_struct *task;

    if(new_task->in_ready_queue) {
        printf("Exceeding the task %p deadline, period: %d, priority: %d\n", new_task, new_task->period, new_task->priority);
        exit(-1);
    }

    new_task->in_ready_queue = TRUE;
    list_for_each_entry(task, queue, list) {
        if(new_task->priority < task->priority) {
            // priority数值越大优先级越小，task插入到pos之后
            list_add_tail(&new_task->list, &task->list);
            return;
        } else if(new_task->priority == task->priority) {
            if(new_task->period < task->period) {
                list_add_tail(&new_task->list, &task->list);
                return;
            }
        }
    }

    // 没有在就绪队列中找到合适的位置（就绪队列中没有其他task），直接插入queue之后
    list_add_tail(&new_task->list, queue);
}

static inline struct task_struct *task_dequeue(struct list_head *queue) 
{
    if (list_empty(queue)) {
        return NULL;
    }

    struct task_struct *task = list_first_entry(queue, struct task_struct, list);
    list_del(&task->list);
    task->in_ready_queue = FALSE;
    return task;
}

static inline void table_scheduler_expiry_point_enqueue(struct scheduler *sched, struct schedule_table *table)
{
    int current_ep_index = (table->next_ep_index - 1 >= 0) ? table->next_ep_index - 1 : table->num_eps - 1;
    struct expiry_point *ep = &table->eps_array[current_ep_index];
    struct task_info *task_info;
    hlist_for_each_entry(task_info, &ep->tasks_hlist, hlist) {
        struct task_struct *task = task_info->task;
        task->task_state = TASK_READY;
        task_enqueue_check(&sched->ready_queue, task);
    }
}

static void table_scheduler_enqueue_task(struct scheduler *sched, struct task_struct *task)
{
    task_enqueue_check(&sched->ready_queue, task);
}

static void table_scheduler_init(struct table_scheduler *table_scheduler, struct task_struct *tasks, const int task_num)
{
    struct expiry_point *eps_hashtable;

    for(int i = 0; i < TABLE_SIZE; ++i) {
        INIT_HLIST_HEAD(&table_scheduler->event_hashtable[i]);
    }

    // 根据tasks的table_id决定table数量
    int num_table = 0;
    for(int i = 0; i < task_num; i++) {
        if(tasks[i].table_id > num_table) {
            num_table = tasks[i].table_id;
        }
    }
    num_table += 1;

    // 确定每个调度表中任务数量和每个调度表的duration
    int duration[num_table], table_task_num[num_table];
    for(int i = 0; i < num_table; i++) {
        duration[i] = 1;
        table_task_num[i] = 0;
    }
    struct task_struct *table_task_array[num_table][task_num];

    for(int i = 0; i < task_num; i++) {
        int table_id = tasks[i].table_id;
        table_task_array[table_id][table_task_num[table_id]] = &tasks[i];
        table_task_num[table_id]++;
        duration[table_id] = LCM(duration[table_id], tasks[i].period);
    }

    // 创建调度表存储空间
    table_scheduler->num_schedule_table = num_table;
    table_scheduler->tables = (struct schedule_table **)malloc(sizeof(struct schedule_table *)*num_table);
    for(int i = 0; i < num_table; i++) {
        table_scheduler->tables[i] = (struct schedule_table *)malloc(sizeof(struct schedule_table));
    }

    for(int i = 0; i < num_table; i++) {
        table_scheduler->tables[i]->duration = duration[i];
        table_scheduler->tables[i]->num_eps = 0;
        // 到期点hash表的桶数一定不会多于duration
        eps_hashtable = (struct expiry_point *)malloc(sizeof(struct expiry_point) * duration[i]);
        for(int j = 0; j < duration[i]; j++) {
            INIT_HLIST_HEAD(&eps_hashtable[j].tasks_hlist);
            INIT_HLIST_HEAD(&eps_hashtable[j].event_tasks_hlist);
            eps_hashtable[j].offset = INVALID_OFFSET;
        }

        // 构建到期点哈希表，并将该到期点的任务放入到期点哈希表
        for(int time = 0; time < duration[i]; time++) {
            for(int j = 0; j < table_task_num[i]; j++) {
                struct task_struct *task = table_task_array[i][j];
                if(time % task->period == 0) {
                    // 当offset为初始值时表示当前到期点还没有插入任何一个任务
                    if(eps_hashtable[time].offset == INVALID_OFFSET) {
                        eps_hashtable[time].offset = time;
                        table_scheduler->tables[i]->num_eps++;    
                    }
                    struct task_info *task_info = (struct task_info *)malloc(sizeof(struct task_info));
                    task_info->task = task;

                    if(task->task_type == BASIC_PERIODIC || task->task_type == BASIC_SINGLE_SHOT) {
                        
                        hlist_add_head(&task_info->hlist, &eps_hashtable[time].tasks_hlist);
                    }

                    if(tasks[i].task_type == EXTENDED_PERIODIC || tasks[i].task_type == EXTENDED_SINGLE_SHOT) {
                        hlist_add_head(&task_info->hlist, &eps_hashtable[time].event_tasks_hlist);
                    }
                }
            }
        }

        // 给调度表创建到期点数组，并用到期点哈希表初始化该数组
        table_scheduler->tables[i]->eps_array = (struct expiry_point *)malloc(sizeof(struct expiry_point) * table_scheduler->tables[i]->num_eps);
        int index = 0;
        for(int key = 0; key < duration[i]; key++) {
            if(eps_hashtable[key].offset != INVALID_OFFSET) {
                table_scheduler->tables[i]->eps_array[index++] = eps_hashtable[key];
            }
        }

        free(eps_hashtable);

        table_scheduler->tables[i]->initial_offset = table_scheduler->tables[i]->eps_array[0].offset;
        table_scheduler->tables[i]->duration = duration[i];
        int last_key = table_scheduler->tables[i]->num_eps - 1;
        table_scheduler->tables[i]->final_delay = duration[i] - table_scheduler->tables[i]->eps_array[last_key].offset;
        table_scheduler->tables[i]->next_ep_index = 0;

        if(table_task_array[i][0]->task_type == BASIC_PERIODIC || 
            table_task_array[i][0]->task_type == EXTENDED_PERIODIC) {
            table_scheduler->tables[i]->table_type = PERIODIC;
        }

        if(table_task_array[i][0]->task_type == BASIC_SINGLE_SHOT || 
            table_task_array[i][0]->task_type == EXTENDED_SINGLE_SHOT) {
            table_scheduler->tables[i]->table_type = SINGLE_SHOT;
        }
    }
}

static inline void delete_all_task_info(struct hlist_head *head) 
{
    struct hlist_node *tmp;
    struct task_info *task_info;

    hlist_for_each_entry_safe(task_info, tmp, head, hlist) {
        hlist_del(&task_info->hlist);
        free(task_info);
    }
}

static void table_scheduler_deinit(struct table_scheduler *table_scheduler)
{
    for(int i = 0; i < table_scheduler->num_schedule_table; i++) {
        for(int j = 0; j < table_scheduler->tables[i]->num_eps; j++) {
            delete_all_task_info(&table_scheduler->tables[i]->eps_array[j].tasks_hlist);
            delete_all_task_info(&table_scheduler->tables[i]->eps_array[j].event_tasks_hlist);
        }     
        
        free(table_scheduler->tables[i]->eps_array);
        free(table_scheduler->tables[i]);
    }

    free(table_scheduler->tables);
}

/*static void set_timer(int fd, long long ns) {
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
}*/

static void set_timer(int fd, long long ns, struct itimerspec *new_timer) 
{
    new_timer->it_value.tv_nsec += ns;
    if (new_timer->it_value.tv_nsec >= 1000000000) {
        new_timer->it_value.tv_sec += 1;
        new_timer->it_value.tv_nsec -= 1000000000;
    }

    timerfd_settime(fd, TFD_TIMER_ABSTIME, new_timer, NULL);
}

static void start_timer(int fd, long long ns, struct itimerspec *new_timer)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    // 初始启动时间
    new_timer->it_interval.tv_sec = 0;
    new_timer->it_interval.tv_nsec = 0; 
    new_timer->it_value = now;
    if(ns == 0)
        new_timer->it_value.tv_nsec += 1;
    else
        new_timer->it_value.tv_nsec += ns;

    timerfd_settime(fd, TFD_TIMER_ABSTIME, new_timer, NULL);
}

/*static void table_scheuler_update_timer(struct schedule_table *table, const struct timespec *fix_start)
{
    int next_ep_index = table->next_ep_index;
    int current_ep_index = next_ep_index != 0 ? next_ep_index - 1 : table->num_eps - 1;
    int next_time;

    printf("current_ep_index: %d, next_ep_index: %d\n", current_ep_index, next_ep_index);
    
    // new_ep_index < old_ep_index表明已经执行完一轮，如果是周期的就开启下一轮，如果是非周期表，就直接停止时钟
    if(table->table_type == PERIODIC) {
        next_time = next_ep_index > current_ep_index ? 
            table->eps_array[next_ep_index].offset - table->eps_array[current_ep_index].offset : table->final_delay + table->initial_offset;
    } else {
        next_time = next_ep_index > current_ep_index ? 
            table->eps_array[next_ep_index].offset - table->eps_array[current_ep_index].offset : 0;
    }

    struct timespec fix_stop;
    clock_gettime(CLOCK_MONOTONIC, &fix_stop);
    long long fix_ns = MS_TO_NS(next_time) - (fix_stop.tv_nsec-fix_start->tv_nsec + (fix_stop.tv_sec - fix_start->tv_sec)*1000000000);
//    long long fix_ns = MS_TO_NS(next_time);
    printf("fix_ns: %lld\n", fix_ns);
    fix_ns > 0 ? set_timer(table->timerfd, fix_ns) : set_timer(table->timerfd, 0);

#ifdef TEST
    clock_gettime(CLOCK_MONOTONIC, &table->start);
#endif
}*/

static void table_scheuler_update_timer(struct schedule_table *table)
{
    int current_ep_index = table->next_ep_index;
    int next_ep_index = table->next_ep_index = (table->next_ep_index + 1) % table->num_eps;
    int next_time;

    printf("current_ep_index: %d, next_ep_index: %d\n", current_ep_index, next_ep_index);
    
    // new_ep_index < old_ep_index表明已经执行完一轮，如果是周期的就开启下一轮，如果是非周期表，就直接停止时钟
    if(table->table_type == PERIODIC) {
        next_time = next_ep_index > current_ep_index ? 
            table->eps_array[next_ep_index].offset - table->eps_array[current_ep_index].offset : table->final_delay + table->initial_offset;
    } else {
        next_time = next_ep_index > current_ep_index ? 
            table->eps_array[next_ep_index].offset - table->eps_array[current_ep_index].offset : 0;
    }

    set_timer(table->timerfd, MS_TO_NS(next_time), &table->new_timer);

#ifdef TEST
    clock_gettime(CLOCK_MONOTONIC, &table->start);
#endif
}

static void table_scheduler_tasks_enqueue(struct scheduler *sched)
{
    struct table_scheduler *table_scheduler = container_of(sched, struct table_scheduler, sched);
    struct schedule_table *table = table_scheduler->tables[table_scheduler->current_table_id];

    table_scheduler_expiry_point_enqueue(sched, table);
}

static void process_tick(struct event_func_args_t *args)
{
    
    struct timespec start, stop;
    clock_gettime(CLOCK_MONOTONIC, &start);
    struct scheduler *sched = &args->scheduler->sched;
    struct table_scheduler *table_scheduler = container_of(sched, struct table_scheduler, sched);
    table_scheduler->current_table_id = args->event_index;
    struct schedule_table *table = args->scheduler->tables[args->event_index];
//    struct task_struct *current_task;
//    const struct timespec *fix_start = (const struct timespec *)args->user_data;
    // 0表示shceduler线程恢复前被抢占，1表示sheduler线程在选择任务时被抢占，2表示sheduler线程在任务运行时被抢占   
    
    uint64_t exp;
    ssize_t s = read(table->timerfd, &exp, sizeof(exp));
    if (s != sizeof(exp)) {
        perror("read timerfd");
    }

    table_scheuler_update_timer(table);

    printf("process_tick begin\n");

    atomic_store(&sched->timing_arrived, TRUE);

    enum sched_state_t scheduler_state = atomic_load(&sched->state);

    if(scheduler_state == SCHED_SUSPENDED) {
        resume_scheduler_thread(sched);
    } else if(scheduler_state == SCHED_WAITING) {
        preempt_task(sched);
    }

    clock_gettime(CLOCK_MONOTONIC, &stop);
    long long elapse = stop.tv_nsec - start.tv_nsec + (stop.tv_sec-start.tv_sec) * 1000000000;

//    if(current_task)
//        printf("process_tick end, current task: %p, period: %d, priority: %d, preempted state: %d, elapse: %lld ns\n", current_task, current_task->period, current_task->priority, preempted_state, elapse);
//    else 
        printf("process_tick end, scheduler state: %d, sched->state: %d, elapse: %lld ns\n", scheduler_state, sched->state, elapse);
//    print_ready_queue(&sched->ready_queue);

    // 更新时钟
//    table_scheuler_update_timer(table, fix_start);
    clock_gettime(CLOCK_MONOTONIC, &process_start);
//    task->sched->sched_class->wake_up_scheduler(task->sched);
}

static void process_task_event(struct event_func_args_t *args)
{
 /*   struct timespec start, stop;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // 处理由task主动让出CPU
    struct scheduler *sched = &args->scheduler->sched;
//    print_ready_queue(&sched->ready_queue);
    sched->current_task = sched->sched_class->pick_next_task(sched);
    if(sched->current_task) {
        printf("process_task_event task: %p, period: %d, priority: %d\n", sched->current_task, sched->current_task->period, sched->current_task->priority);
        if(!sched->current_task->is_preempted) {
            // 任务并非被抢占的，说明是被条件变量阻塞的
            sched->current_task->task_state = RESUMING;
            resume_task(sched->current_task);

            clock_gettime(CLOCK_MONOTONIC, &stop);
            long long elapse = stop.tv_nsec - start.tv_nsec + (stop.tv_sec-start.tv_sec) * 1000000000;
            printf("process_task_event not preempted routine elapse: %lld ns\n", elapse);
        } else {
            // 任务是被抢占的
            sched->current_task->is_preempted = 0;
            resume_preempted_task(sched->current_task);
            
            clock_gettime(CLOCK_MONOTONIC, &stop);
            long long elapse = stop.tv_nsec - start.tv_nsec + (stop.tv_sec-start.tv_sec) * 1000000000;
            printf("process_task_event preempted routine elapse: %lld ns\n", elapse);
        } 
//        sched->sched_class->wake_up_scheduler(sched->current_task->sched);
    }

    clock_gettime(CLOCK_MONOTONIC, &process_stop);
    process_elapse = process_stop.tv_nsec - process_start.tv_nsec + (process_stop.tv_sec-process_start.tv_sec) * 1000000000;
    printf("task and schedule total elapse: %lld\n", process_elapse);*/
}

static void process_isr_event(struct event_func_args_t *args)
{

}

static void register_event_table(int fd, int event_index, process_event_func pfunc, struct table_scheduler *scheduler)
{
    int index = fd % TABLE_SIZE;
    struct event_table_node *node = (struct event_table_node *)malloc(sizeof(struct event_table_node));
    node->fd = fd;
    node->p_event_func = pfunc;
    node->args.scheduler = scheduler;
    node->args.event_index = event_index;
    hlist_add_head(&node->hlist, &scheduler->event_hashtable[index]);
}

static void unregister_event_table(int fd, struct table_scheduler *scheduler) 
{
    int index = fd % TABLE_SIZE;
    struct hlist_head *head = &scheduler->event_hashtable[index];
    struct hlist_node *tmp;
    struct event_table_node *node;

    hlist_for_each_entry_safe(node, tmp, head, hlist) {
        if(node->fd == fd) {
            hlist_del(&node->hlist);
            free(node);
            break;
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

    // 事件数量为每个调度表时钟数量+本地事件源数量
    table_scheduler->num_events = table_scheduler->num_schedule_table + NATIVE_EVENT_NUM;
    table_scheduler->event_fds = (int *)malloc(sizeof(int) * table_scheduler->num_events);

    struct epoll_event event;

    for(int i = 0; i < table_scheduler->num_schedule_table; i++) {
        // 创建table scheduler的timer
        struct schedule_table *table = table_scheduler->tables[i];
        table->timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
        if(table->timerfd == -1) {
            printf("Create scheduler timer fail!\n");
            goto create_timer_fail;
        }
        table_scheduler->event_fds[i] = table->timerfd;
        
        event.events = EPOLLIN;
        event.data.fd = table_scheduler->event_fds[i];

        // 注册event process handle给event table
        register_event_table(event.data.fd, i, process_tick, table_scheduler);

        // 添加event到epoll中
        if (epoll_ctl(table_scheduler->epoll_fd, EPOLL_CTL_ADD, table_scheduler->event_fds[i], &event) == -1) {
            printf("Add event %d fail\n", i);
            goto create_eventfd_fail;
        }
    }

    for(int i = table_scheduler->num_schedule_table + NATIVE_EVENT_TASK; i < table_scheduler->num_events; i++) {
        // 创建native event fd
        table_scheduler->event_fds[i] = eventfd(0, EFD_NONBLOCK);
        if(table_scheduler->event_fds[i] == -1) {
            printf("create fd of event %d fail\n", i);
            goto create_eventfd_fail;
        }

        event.events = EPOLLIN | EPOLLET;
        event.data.fd = table_scheduler->event_fds[i];

        if(i == table_scheduler->num_schedule_table + NATIVE_EVENT_TASK) {
            register_event_table(event.data.fd, i, process_task_event, table_scheduler);
        } else {
            register_event_table(event.data.fd, i, process_isr_event, table_scheduler);
        }

        if (epoll_ctl(table_scheduler->epoll_fd, EPOLL_CTL_ADD, table_scheduler->event_fds[i], &event) == -1) {
            printf("Add event %d fail\n", i);
            goto create_eventfd_fail;
        }
    }

    return RET_SUCCESS;

create_eventfd_fail:
    for(int event_index = 0; event_index < table_scheduler->num_events; event_index++) {
        if(table_scheduler->event_fds[event_index] != -1) {
            unregister_event_table(table_scheduler->event_fds[event_index], table_scheduler);
            close(table_scheduler->event_fds[event_index]);
        }
    }

create_timer_fail:
    free(table_scheduler->event_fds);
    close(table_scheduler->epoll_fd);

    return RET_FAIL;
}

static void table_scheduler_destroy_epoll(struct table_scheduler *table_scheduler)
{
    for(int event_index = 0; event_index < table_scheduler->num_events; event_index++) {
        unregister_event_table(table_scheduler->event_fds[event_index], table_scheduler);
        close(table_scheduler->event_fds[event_index]);
    }
    free(table_scheduler->event_fds);
    close(table_scheduler->epoll_fd);
}

static struct scheduler *table_scheduler_create(struct sched_class *sched_class, int cpu, struct task_struct *tasks, int task_num)
{
    struct table_scheduler *table_scheduler = (struct table_scheduler *)malloc(sizeof(struct table_scheduler));
    struct scheduler *sched = &table_scheduler->sched;
    
    // 初始化调度器（通用）
    init_scheduler(sched, sched_class, cpu, tasks, task_num);

    // 根据任务初始化调度表
    table_scheduler_init(table_scheduler, sched->tasks, sched->task_num);
    
    // 创建调度器的epoll
    int ret = table_scheduler_create_epoll(table_scheduler);
    if(ret == -1) {
        goto create_epoll_fail;
    }
    
    if(scheduler_create_threads(sched) == RET_FAIL) {
        printf("Create threads on scheduler fail!\n");
        goto scheduler_create_threads_fail;
    }
    
    return (struct scheduler *)table_scheduler;

    //失败后的资源销毁
scheduler_create_threads_fail:
    table_scheduler_destroy_epoll(table_scheduler);

create_epoll_fail:
    table_scheduler_deinit(table_scheduler);
    deinit_scheduler(sched);
    free(table_scheduler);
    return NULL;
}

static void table_scheduler_destroy(struct scheduler *sched)
{
    struct table_scheduler *table_scheduler = container_of(sched, struct table_scheduler, sched);

    scheduler_join_threads(sched);

    deinit_scheduler(sched);

    table_scheduler_destroy_epoll(table_scheduler);
    
    table_scheduler_deinit(table_scheduler);

    free(table_scheduler);
}

static void table_scheduler_start(struct scheduler *sched)
{
    struct table_scheduler *table_sched = container_of(sched, struct table_scheduler, sched);
    for(int i = 0; i < table_sched->num_schedule_table; i++) {
        start_timer(table_sched->tables[i]->timerfd, MS_TO_NS(table_sched->tables[i]->initial_offset), &table_sched->tables[i]->new_timer);

#ifdef TEST
        table_sched->tables[i]->current_elapse = table_sched->tables[i]->initial_offset;
        clock_gettime(CLOCK_MONOTONIC, &table_sched->tables[i]->start);
#endif
    }
}

static void table_scheduler_schedule(struct scheduler *sched)
{
//    uint64_t expirations;
    struct table_scheduler *table_sched = container_of(sched, struct table_scheduler, sched);
    int num_events = table_sched->num_events;  

    struct epoll_event events[num_events];
    
    int n = epoll_wait(table_sched->epoll_fd, events, num_events, -1);
    
    printf("scheduler on cpu: %d, the number of events to respond to is %d\n", sched->cpu, n);

//    struct timespec fix_start;
//    clock_gettime(CLOCK_MONOTONIC, &fix_start);

#ifdef TEST
    struct timespec stop;
    clock_gettime(CLOCK_MONOTONIC, &stop);
    float elapse = (stop.tv_nsec - table->start.tv_nsec) / (float)1000000;
    if(old_ep_index > 0) {
        table->current_elapse = table->eps[old_ep_index].offset - table->eps[old_ep_index-1].offset;
        printf("Table theoretical elapse: %d, real elapse: %.2f\n", table->current_elapse, elapse);
    }
#endif
    for(int i = 0; i < n; i++) {
        int fd = events[i].data.fd;
        int index = fd % TABLE_SIZE; 
        struct hlist_head *head = &table_sched->event_hashtable[index];
        struct event_table_node *node;

        //使用表驱动访问对应事件的处理函数
        hlist_for_each_entry(node, head, hlist) {
            if(node->fd == fd) {
//                node->args.user_data = &fix_start;
                node->p_event_func(&node->args);
                break;
            }
        }
    }
}

static struct task_struct *table_scheduler_pick_next_task(struct scheduler *sched)
{
    return task_dequeue(&sched->ready_queue);
}

static void table_scheduler_wake_up(struct scheduler *sched)
{
    sched->current_task = NULL;
    struct table_scheduler *table_sched = container_of(sched, struct table_scheduler, sched);
    uint64_t u = 1;
    ssize_t s = write(table_sched->event_fds[table_sched->num_schedule_table + NATIVE_EVENT_TASK], &u, sizeof(uint64_t));
    if (s != sizeof(uint64_t)) {
        perror("write");
    }
}

struct sched_class table_sched_class = {
    .scheduler_create = table_scheduler_create,
    .scheduler_destroy = table_scheduler_destroy,
    .scheduler_start = table_scheduler_start,
    .schedule = table_scheduler_schedule,
    .pick_next_task = table_scheduler_pick_next_task,
    .wake_up_scheduler = table_scheduler_wake_up,
    .enqueue = table_scheduler_tasks_enqueue,
    .enqueue_task = table_scheduler_enqueue_task,
};