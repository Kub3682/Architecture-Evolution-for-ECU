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

这段代码并不是真正的 RTOS，因为它没有实现“实时性”要求。
    任务没有“在规定的时间运行”
        它采用的是协作式调度，任务必须主动让出 CPU（switch_context()）。
        但真正的 RTOS 需要抢占式调度，通常通过定时器中断强制切换任务。
    任务没有“在规定的时长运行结束”
        代码中任务 task1() 和 task2() 运行时间不可控，只能靠它们自己调用 switch_context() 切换任务。
        但 RTOS 通常使用 时间片（time slice） 或 优先级调度 来确保任务在规定的时间内运行。

一个简单的基于任务切换的调度器（简单的协作式多任务调度），它模仿了一个最基础的RTOS（实时操作系统，极简版的操作系统）的任务管理机制，
允许多个任务（task）通过上下文切换（context switch）来执行。代码中涉及结构体、函数指针、内联汇编、内存分配和上下文切换等概念
设计有栈管理、任务调度

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

*/

#include <stdint.h>  // 定义标准整数类型，如 uint32_t(无符号 32 位整数（unsigned int）), uint8_t
#include <stdbool.h> // 定义 bool 类型（true/false代替 1 和 0）
#include <stdlib.h> // 提供 malloc() 动态内存分配函数

#define MAX_TASKS 10 // 定义最大任务数,即调度器最多能管理 10 个任务。
#define STACK_SIZE 256 // 定义每个任务栈的大小（单位：字节）假设每个任务的栈有 256 字节

//定义任务结构体,表示一个任务，每个任务有两个主要属性
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
    *(--tasks[taskCount].stackPointer) = 0xFFFFFFF0; // xPSR（程序状态寄存器）默认值 0xFFFFFFF0（用于 Cortex-M 处理器，这是ARM Cortex-M的伪代码）

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
void task1(void){
    while(1){
        // 此处编写任务1的代码。。。
        switch_context(); // 切换上下文到下一个任务，主动让出工作台（协作式调度，任务主动让出CPU（对比抢占式需要定时器中断））
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