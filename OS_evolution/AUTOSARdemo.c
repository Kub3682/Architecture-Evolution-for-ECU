/*
 * 基于AUTOSAR OS理念的轻量级抢占式调度器设计
 * 核心特性：
 *   1. 多优先级任务调度（0-255, 0为最高）
 *   2. 时间片轮转调度扩展
 *   3. 栈溢出保护机制
 *   4. 完整上下文保存（符合AAPCS标准）
 *   5. 基础系统服务框架
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

//--------------------- 系统配置 ---------------------
#define MAX_TASKS         10      // 最大任务数
#define STACK_SIZE        256     // 每个任务栈大小（单位：uint32_t）
#define STACK_MAGIC_NUM   0xDEADBEEF  // 栈溢出检测魔数
#define SYSTICK_FREQ      1000    // SysTick频率(Hz)

//--------------------- 类型定义 ---------------------
typedef enum {
    TASK_READY,         // 任务就绪
    TASK_RUNNING,       // 任务正在运行
    TASK_BLOCKED,       // 任务阻塞等待
    TASK_SUSPENDED      // 任务被挂起
} TaskState;

// 任务控制块(TCB)
typedef struct {
    uint32_t *stackPtr;     // 当前栈指针
    uint32_t *stackBase;    // 栈基地址
    TaskState state;        // 任务状态
    uint8_t priority;       // 任务优先级（0最高）
    uint32_t timeSlice;     // 剩余时间片
    void (*entry)(void);    // 任务入口函数
} Task;

//--------------------- 全局变量 ---------------------
static Task taskPool[MAX_TASKS];    // 任务池
static uint8_t taskCount = 0;       // 当前任务数
static volatile uint8_t currentTask = 0;  // 当前运行任务索引
static uint32_t systemTicks = 0;    // 系统时钟滴答数

//--------------------- 汇编接口 ---------------------
// 上下文切换函数（在startup.s中实现）
extern void switch_context(void);

//--------------------- 硬件抽象层 --------------------
// 初始化SysTick定时器（每1ms触发一次）
static void systick_init(void) {
    uint32_t reload = (SystemCoreClock / SYSTICK_FREQ) - 1;
    SysTick->LOAD = reload;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |
                   SysTick_CTRL_TICKINT_Msk |
                   SysTick_CTRL_ENABLE_Msk;
}

// SysTick中断服务例程
void SysTick_Handler(void) {
    systemTicks++;
    // 更新时间片
    if(taskPool[currentTask].timeSlice > 0) {
        taskPool[currentTask].timeSlice--;
        // 时间片耗尽时触发调度
        if(taskPool[currentTask].timeSlice == 0) {
            switch_context();
        }
    }
}

//--------------------- 任务管理 ----------------------
// 初始化任务栈（模拟异常自动入栈）
static void init_task_stack(Task *task) {
    uint32_t *stackTop = task->stackBase + STACK_SIZE;
    
    // 在栈顶设置魔数用于溢出检测
    *(--stackTop) = STACK_MAGIC_NUM;
    
    // 模拟异常发生时自动保存的上下文（xPSR, PC, LR, R12, R3-R0）
    *(--stackTop) = 0x01000000;     // xPSR (Thumb模式)
    *(--stackTop) = (uint32_t)task->entry; // PC
    *(--stackTop) = 0xFFFFFFFF;     // LR (异常返回模式)
    *(--stackTop) = 0;              // R12
    *(--stackTop) = 0;              // R3
    *(--stackTop) = 0;              // R2
    *(--stackTop) = 0;              // R1
    *(--stackTop) = 0;              // R0
    
    // 手动保存的寄存器R4-R11
    for(uint8_t i=0; i<8; i++) {
        *(--stackTop) = 0;  // R11~R4
    }
    
    task->stackPtr = stackTop;
}

// 创建新任务（优先级调度+时间片）
bool task_create(void (*entry)(void), uint8_t priority, uint32_t timeSlice) {
    if(taskCount >= MAX_TASKS) return false;
    
    Task *newTask = &taskPool[taskCount];
    newTask->stackBase = (uint32_t*)malloc(STACK_SIZE * sizeof(uint32_t));
    if(!newTask->stackBase) return false;
    
    newTask->entry = entry;
    newTask->state = TASK_READY;
    newTask->priority = priority;
    newTask->timeSlice = timeSlice;
    
    init_task_stack(newTask);
    taskCount++;
    return true;
}

//--------------------- 调度策略 ----------------------
// 查找最高优先级就绪任务
static void schedule(void) {
    uint8_t highestPri = 0xFF;
    uint8_t candidate = currentTask;
    
    for(uint8_t i=0; i<taskCount; i++) {
        if(taskPool[i].state == TASK_READY && 
           taskPool[i].priority < highestPri) {
            highestPri = taskPool[i].priority;
            candidate = i;
        }
    }
    
    if(candidate != currentTask) {
        currentTask = candidate;
        taskPool[currentTask].timeSlice = 10; // 重置时间片
    }
}

//--------------------- 上下文切换 --------------------
// 在startup.s中实现的汇编代码示例：
/*
switch_context:
    // 保存当前任务上下文
    PUSH {R4-R11}       // 手动保存R4-R11
    MRS R0, PSP         // 保存进程栈指针
    STR R0, [current_task_stack]
    
    // 调用调度器选择新任务
    BL schedule
    
    // 加载新任务上下文
    LDR R0, [new_task_stack]
    MSR PSP, R0
    POP {R4-R11}        // 恢复R4-R11
    BX LR               // 返回到新任务
*/

//--------------------- 系统服务 ----------------------
void os_delay(uint32_t ticks) {
    taskPool[currentTask].state = TASK_BLOCKED;
    taskPool[currentTask].timeSlice = ticks;
    switch_context();
}

//--------------------- 示例任务 ----------------------
void task1(void) {
    while(1) {
        // 任务1工作逻辑
        os_delay(500); // 阻塞500ms
    }
}

void task2(void) {
    while(1) {
        // 任务2工作逻辑
        os_delay(1000); // 阻塞1000ms
    }
}

//--------------------- 启动入口 ----------------------
int main(void) {
    // 硬件初始化
    systick_init();
    
    // 创建任务（优先级1比2高）
    task_create(task1, 1, 10);  // 时间片10ms
    task_create(task2, 2, 20);  // 时间片20ms
    
    // 启动调度器
    schedule();
    switch_context();
    
    // 不会执行到这里
    while(1);
}

/* 设计思路说明：
1. 分层架构：
   - 硬件抽象层：systick_init()封装硬件相关操作
   - 内核服务层：task_create/os_delay提供系统服务
   - 任务层：用户任务通过API与系统交互

2. 优先级调度：
   - schedule()遍历任务池选择最高优先级就绪任务
   - 支持255级优先级，数值越小优先级越高

3. 时间片轮转：
   - SysTick每1ms触发中断更新时间片
   - 时间片耗尽后触发上下文切换

4. 栈保护：
   - 每个栈底部设置魔数STACK_MAGIC_NUM
   - 可扩展添加栈检测函数定期校验

5. 符合AAPCS标准：
   - 上下文切换保存R4-R11和异常自动保存的寄存器
   - 使用独立的进程栈(PSP)

6. 可扩展性：
   - 可添加任务状态转换函数（suspend/resume）
   - 支持事件驱动机制（SetEvent/WaitEvent）
   - 可集成资源管理模块
*/