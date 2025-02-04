/*一个简单的基于任务切换的调度器，它模仿了一个最基础的RTOS（实时操作系统）的任务管理机制，
允许多个任务（task）通过上下文切换（context switch）来执行。设计有栈管理、任务调度、汇编上下文切换、函数指针*/

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

// 初始化一个任务,void (*task_func)(void)：函数指针，指向任务的入口函数（即任务执行的代码）
bool create_task(void (*task_func)(void)) {
    if(taskCount >= MAX_TASKS) return false; // 检查是否超过最大任务数

    // 为新任务分配栈空间
    tasks[taskCount].stackBase = (uint32_t*)malloc(STACK_SIZE *sizeof(uint32_t));
    tasks[taskCount].stackPointer = tasks[taskCount].stackBase + STACK_SIZE / sizeof(uint32_t) - 1;

    // 为新任务设置初始栈植
    *(tasks[taskCount].stackPointer) = (uint32_t)(task_func); // 程序计数器（PC）
    *(--tasks[taskCount].stackPointer) = 0xFFFFFFF0; // xPSR（程序状态寄存器）默认值 0xFFFFFFF0（用于 Cortex-M 处理器）

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
        switch_context(); // 切换上下文到下一个任务
    }
}

// 示例任务2
void task2(void){
    while(1){
        // 此处编写任务2的代码。。。
        switch_context(); // 切换上下文到下一个任务
    }
}

// 启动RTOS的主函数,创建两个任务,无限循环，不断进行任务切换
int main(void){
    create_task(task1); // 创建第一个任务
    create_task(task2); // 创建第二个任务
    while (1)
    {
        switch_context(); // 启动调度过程，切换上下文
    }
    return 0;
    
}