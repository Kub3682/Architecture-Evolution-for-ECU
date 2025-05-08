#ifndef _SIMULATOR_H_
#define _SIMULATOR_H_

enum task_type_t {
    BASIC_PERIODIC,
    BASIC_SINGLE_SHOT,
    EXTENDED_PERIODIC,
    EXTENDED_SINGLE_SHOT,
    ISR,
    UNKNOWN_TASK_TYPE,
};

enum schedule_strategy_t {
    TABLE,
    FIFO,
    RR,
    NUM_SCHED_STRATEGY,
};

typedef void (*task_function)(void);

struct task_struct_info {
    int priority;
    int cpu;
    int period;
    int table_id;
    enum task_type_t task_type;
    task_function pfunc;
};

void create_simulator(struct task_struct_info *task_info_arr, int num, enum schedule_strategy_t sched_strategy);
void run_simulator();
void destroy_simulator();

#endif
