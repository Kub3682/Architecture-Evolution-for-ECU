#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#define MAX_TASKS 10 // 定义最大任务数
#define STACK_SIZE 256 // 定义每个任务栈的大小

//定义任务结构体
typedef struct Task
{
    unint32_t *stackPointer; // 指向任务栈的指针
    unint32_t *stackBase; // 任务栈的基地址
};

static Task tasks[MAX_TASKS]; // 定义任务数组
static uint8_t currentTask = 0; // 当前任务索引
static uint8_t taskCount = 0; // 当前任务数量

// 函数原型
void scheduler(void);
void switch_context(void);

// 初始化一个任务
bool create_task(void (*task_func)(void)) {
    if(taskCount >= MAX_TASKS) return false; // 检查是否超过最大任务数

    // 为新任务分配栈空间
    tasks[taskCount].stackBase = (uint32_t*)malloc(STACK_SIZE *sizeof(uint32_t));
    tasks[taskCount].stackPointer = tasks[taskCount].stackBase + STACK_SIZE / sizeof(uint32_t) - 1;

    // 为新任务设置初始栈植
    *(tasks[taskCount].stackPointer) = (uint32_t)(task_func); // 程序计数器（PC）
    *(--tasks[taskCount].stackPointer) = 0xFFFFFFF0; // xPSR (默认值)

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

// 启动RTOS的主函数
int main(void){
    create_task(task1); // 创建第一个任务
    create_task(task2); // 创建第二个任务
    while (1)
    {
        switch_context(); // 启动调度过程，切换上下文
    }
    return 0;
    
}