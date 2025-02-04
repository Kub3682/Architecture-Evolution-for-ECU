/*
目标：
    在单核 CPU 上实现一个简单的任务调度系统（RTOS 的雏形），让多个任务交替执行，而不使用多线程。
基本需求：
    多任务管理
        需要支持多个任务，每个任务有自己的栈空间。
        需要存储所有任务的信息（任务结构体）。
        任务切换机制（核心逻辑）
    需要切换任务上下文，即切换 CPU 寄存器、栈指针（SP）、程序计数器（PC）。
    任务切换必须高效，C 语言无法直接操作寄存器，因此需要 汇编。
    调度策略
        采用 简单的轮询调度（Round-Robin）：任务按顺序执行，每次调用 switch_context() 切换到下一个任务。
    时间轴            | 栈状态                     | 执行流
-----------------------------------------------------------------
main()初始化后         | [main栈][task1栈][task2栈] | 执行main的while循环
第一次switch_context   | 保存main栈 → 加载task1栈 | 跳转到task1()
task1运行             | [main栈][task1栈][task2栈] | 执行task1代码
task1调用switch_context | 保存task1栈 → 加载task2栈 | 跳转到task2()
task2运行              | [main栈][task1栈][task2栈] | 执行task2代码
...循环往复...

这段代码并不是真正的 RTOS，因为它没有实现“实时性”要求。
    任务没有“在规定的时间运行”
        它采用的是协作式调度，任务必须主动让出 CPU（switch_context()）。
        但真正的 RTOS 需要抢占式调度，通常通过定时器中断强制切换任务。
    任务没有“在规定的时长运行结束”
        代码中任务 task1() 和 task2() 运行时间不可控，只能靠它们自己调用 switch_context() 切换任务。
        但 RTOS 通常使用 时间片（time slice） 或 优先级调度 来确保任务在规定的时间内运行。

一个简单的基于任务切换的调度器（简单的协作式多任务调度），它模仿了一个最基础的RTOS（实时操作系统，极简版的操作系统）的任务管理机制，
允许多个任务（task）通过上下文切换（context switch）来执行。代码中涉及结构体、函数指针、内联汇编、内存分配和上下文切换等概念
设计有栈管理、任务调度。
这段代码的主要目标是实现一个非常基础的协作式任务调度器，并通过上下文切换来模拟多任务并发。其核心特点在于：
    稳定性：通过任务结构体、栈管理和上下文切换机制，确保任务能够正确保存和恢复。
    灵活性：通过任务创建接口、函数指针和可替换的调度策略，保证系统在未来能够适应更多的任务和复杂的调度需求。

switch_context() 必须使用汇编，因为：
    需要直接操作 CPU 寄存器（SP、LR）。
    需要保存/恢复任务的栈，以便任务切换。
    C 语言无法完成 这些低级操作，必须用汇编精确控制任务切换。

存在问题与改进方向
    错误处理：当前代码缺少错误检查（如malloc失败）
    寄存器保存不完整：实际需要保存R4-R11等更多寄存器
    没有优先级：简单轮转调度无法处理紧急任务
    汇编移植性：内联汇编依赖特定编译器（如GCC）和架构（ARM）
    栈初始化简化：真实的上下文初始化需要更多寄存器的初始化

理解这段代码后，可以继续研究：
    FreeRTOS或uC/OS等真实RTOS的实现
    硬件中断与抢占式调度
    任务间通信（信号量、队列等）
    内存保护机制

协作式调度（Cooperative Scheduling）的概念，它依赖任务自身主动调用 switch_context() 让出 CPU，
不同于抢占式调度（Preemptive Scheduling），抢占式调度通常需要硬件定时器中断来打断任务执行。
抢占式调度（如 FreeRTOS）通常：
    使用定时器中断（比如SysTick 定时器）。
    任务不需要主动调用 switch_context()，而是定时器中断会打断任务，自动切换任务。

抢占式调度：像被闹钟强行打断当前工作
协作式调度：像使用番茄工作法，每个25分钟主动休息切换任务

*/

#include <stdint.h>  // 定义标准整数类型，如 uint32_t(无符号 32 位整数（unsigned int）), uint8_t
#include <stdbool.h> // 定义 bool 类型（true/false代替 1 和 0）
#include <stdlib.h> // 提供 malloc() 动态内存分配函数

#define MAX_TASKS 10 // 定义最大任务数,即调度器最多能管理 10 个任务。
#define STACK_SIZE 256 // 定义每个任务栈的大小（单位：字节）假设每个任务的栈有 256 字节

//定义任务结构体,表示一个任务对象，每个任务有两个主要属性
typedef struct Task
{
    uint32_t *stackPointer; // 指向任务栈的指针
    uint32_t *stackBase; // 任务栈的基地址
};

static Task tasks[MAX_TASKS]; // 定义任务数组,用数组存储所有的任务信息，每个任务对应一个 Task 结构体
static uint8_t currentTask = 0; // 当前正在运行的任务索引（任务编号）
static uint8_t taskCount = 0; // 当前总共有多少个任务被创建

// 函数原型
void scheduler(void);
void switch_context(void);

// 初始化一个任务,void (*task_func)(void)：函数指针，指向任务的入口函数（即任务执行的代码），通过task_func将代码位置存入栈，实现任务跳转
bool create_task(void (*task_func)(void)) {  //task_func 是一个指针变量（它存储一个函数的地址），它指向某个函数（指向的函数没有参数，返回类型是 void）
    if(taskCount >= MAX_TASKS) return false; // 检查是否超过最大任务数

    // 为新任务分配栈空间，栈指针指向工作台末尾（栈从高地址向低地址增长）
    tasks[taskCount].stackBase = (uint32_t*)malloc(STACK_SIZE *sizeof(uint32_t));
    tasks[taskCount].stackPointer = tasks[taskCount].stackBase + STACK_SIZE / sizeof(uint32_t) - 1;

    // 为新任务设置初始栈植
    *(tasks[taskCount].stackPointer) = (uint32_t)(task_func); // 程序计数器（PC）指针指向任务函数
    *(--tasks[taskCount].stackPointer) = 0xFFFFFFF0; // xPSR（Program Status Register程序状态寄存器）默认值 0xFFFFFFF0（用于 Cortex-M 处理器，这是ARM Cortex-M的伪代码）

    taskCount++; // 增加当前任务数量
    return true;
}

// 简单的轮询调度器
void scheduler(void){
    currentTask++; // 切换到下一个任务
    if(currentTask >= taskCount){
        currentTask = 0; // 如果超过最大数量，则重置为任务0
    }
}

// 上下文切换函数
//一般寄存器（R0 - R12），这些寄存器用于存储任务的局部变量、函数参数、临时数据等。当任务切换时，当前任务的寄存器值需要被保存到栈中，以便任务恢复时能够继续使用正确的值。
//链接寄存器（LR，Link Register），链接寄存器用于保存函数返回地址。当函数调用时，LR 保存函数返回的地址。任务栈会保存链接寄存器的值，以便任务执行完一个函数后能够正确地返回。
void switch_context(void){
    __asm volatile(
        "PUSH {R0-R3, R12} \n"  // 保存寄存器
        "LDR R0, =tasks \n" // 加载任务数组的地址
        "LDR R1, =currentTask \n" // 加载当前任务索引
        "LDR R2, [R1] \n" // 加载当前任务索引值
        "LDR R3, [R0, R2, LSL #3] /n" // 加载当前任务结构体
        "STR SP, [R3] \n" // 保存当前栈指针
        "BL scheduler \n" // 调用调度器
        "LDR R2, [R1] \n" // 加载新的当前任务索引值
        "LDR R3, [R0, R2, LSL #3] /n" // 加载新的任务结构体
        "LDR SP, [R3] \n" // 恢复新的栈指针
        "POP {R0-R3, R12} \n" // 恢复寄存器
        "BX LR \n" // 从中断/异常返回
    );
}

// 示例任务1
/* 
第一次执行时：
任务1从头开始执行逻辑1，执行到switch_context()时挂起。
然后会调度到其他任务（比如任务2）。
任务1重新调度回来时：
任务1恢复执行，上次中断的位置是switch_context()后的代码，即执行逻辑2。
所以，任务1的逻辑2会在任务1再次被调度回来时继续执行。
任务1的**逻辑1和逻辑2都会被执行**，只是它们的执行过程被调度器分割成多个片段（每次切换任务时保存当前执行状态）。
*/
void task1(void){
    while(1){
        // 此处编写任务1的代码逻辑1
        switch_context(); // 切换上下文到下一个任务，主动让出工作台（协作式调度，任务主动让出CPU（对比抢占式需要定时器中断））
        // 此处编写任务1的代码逻辑2
    }
}

// 示例任务2
void task2(void){
    while(1){
        // 此处编写任务2的代码。。。
        switch_context(); // 切换上下文到下一个任务，主动让出工作台（协作式调度，任务主动让出CPU（对比抢占式需要定时器中断））
    }
}

// 启动RTOS的主函数,创建两个任务,无限循环，不断进行任务切换
int main(void){
    create_task(task1); // 创建第一个任务，分配了任务 1 的栈空间，并在栈顶保存了 task1() 的入口地址
    create_task(task2); // 创建第二个任务，分配了任务 2 的栈空间，并在栈顶保存了 task2() 的入口地址
    while (1) // 不断调用 switch_context()，在任务间切换
    {
        switch_context(); // 启动调度过程，切换上下文，开始轮转调度
    }
    return 0;
    
}


/*
从设计角度来说，函数的命名应该能够准确地反映其核心功能，而不应该让人产生混淆。
switch_context() 这个名字的确更侧重于具体的上下文切换操作，而整个轮询调度的核心功能其实是任务调度。
如果将 main() 函数命名为 switch_context()，确实可能给人误导，容易让人认为这个函数的主要目标是上下文切换，而不是任务调度。
为了让代码更具可读性和直观性，可以考虑将任务调度的核心函数命名为更清晰的名称，
例如task_scheduler()，这样人们一眼就能看出它的主要功能是轮询任务调度而不仅仅是上下文切换。

另外，将 scheduler(void) 改名为 scheduler_strategy(void)，则能够更明确地传达出它是一个策略选择的函数，
适用于将来可能进行的不同调度策略（比如优先级调度、时间片调度等）的切换。

设计层面优化
（1）分离任务管理与调度策略
当前 scheduler() 直接对 currentTask 进行简单的递增轮询，这在目前只有两个任务时没有问题，但如果要引入优先级调度、时间片轮转或任务动态创建/删除，那么调度策略需要更灵活。

改进建议：

采用独立的调度策略模块，使用函数指针方式，使 task_scheduler() 只负责调用 scheduler_strategy()，而 scheduler_strategy() 由用户可配置：

typedef uint8_t (*scheduler_strategy_t)(void);
static scheduler_strategy_t scheduler_strategy = round_robin_scheduler;

允许更换调度策略，例如：
round_robin_scheduler() （轮询）
priority_scheduler() （基于优先级）
time_slice_scheduler() （时间片调度）
event_driven_scheduler() （基于事件）

比如：

void priority_scheduler(void) {
    uint8_t highestPriority = 255;
    uint8_t nextTask = currentTask;

    for (uint8_t i = 0; i < taskCount; i++) {
        if (tasks[i].state == TASK_READY && tasks[i].priority < highestPriority) {
            highestPriority = tasks[i].priority;
            nextTask = i;
        }
    }

    currentTask = nextTask;
}

void time_slice_scheduler(void) {
    static uint32_t tick_count = 0;
    tick_count++;
    
    if (tasks[currentTask].timeSlice == 0 || tick_count % tasks[currentTask].timeSlice == 0) {
        do {
            currentTask = (currentTask + 1) % taskCount;
        } while (tasks[currentTask].state != TASK_READY);
    }
}


（2）增加任务控制块（TCB）
当前的 Task 结构体仅存储栈指针，但真实 RTOS 需要存储更多任务信息（如任务状态、优先级、时间片等）。
优化 Task 结构体：

typedef enum {TASK_READY, TASK_RUNNING, TASK_BLOCKED, TASK_SUSPENDED} TaskState;
typedef struct {
    uint32_t *stackPointer; // 任务栈指针
    uint32_t *stackBase;    // 任务栈基地址
    TaskState state;        // 任务状态
    uint8_t priority;       // 任务优先级（用于优先级调度）
    uint32_t timeSlice;     // 时间片（用于时间片调度）
} Task;



*/