### 软件架构设计
本项目围绕 **AUTOSAR Classic Platform 的任务调度模型** 构建，用于仿真AUTOSAR Classic Platform中任务调度和任务执行流程。

- 

根据上述逻辑，本项采用模块化设计，其核心逻辑由四个关键模块组成：

本小节将讨论任务管理的软件架构设计，它会基于现有AUTOSAR Classic Platform标准，但是，作为仿真器的核心组件，它必须面向通用OS进行扩展设计。首先，对其关键组件的功能以及它们之间的关系进行设计：

- 任务管理方法（调度方法）：主要实现任务活动、任务状态、优先级、资源、中断、错误等执行行为。这些行为会根据具体操作系统遵循的规则、所选择的调度策略发生变化。例如，AUTOSAR OS要求设计时静态配置任务，不会在运行时创建新任务，它不会直接向用户或者其他操作系统模块提供诸如fork或者CreateTask类似的函数或方法。相反，很多通用操作系统或者支持动态创建任务的RTOS均提供这类方法。另外，不同调度策略对调度函数的具体函数也不尽相同。因此，需要划分任务管理方法中的不变量和变量，以应对不同更为广泛的情况和需求变化。具体划分为：
  + 不变量：任务管理方法向外提供的基本功能是不变的。尽管不同OS所支持的任务行为种类不尽相同（或向外的函数名不同）或者底层实现方法不同，但是这些行为通常是互为子集关系且大部分向外部提供的功能表现是基本一致的。
  + 变量：具体功能数量和底层实现逻辑
根据上述分析，可利用“接口隔离”原则，实现这些不变量与变量的分离：
  + 抽象的任务管理接口：设计抽象接口，该接口是任务管理行为各行为的超集。接口用于用户和OS的外部调用。
  + 具体的任务管理方法：根据自身OS、调度方法设计具体的任务管理方法，然后绑定给接口调用。

- 支持任务管理的结构（调度器结构）：主要为任务管理方法提供数据结构支持。该部分同样存在公共的不变量，例如调度器所依赖的时钟、调度器所在核心等。也同样存在变量，具体调度方法实现所用的数据结构，例如，表调度、FIFO、RR所使用的列表（或队列），完全公平调度用到的红黑树等。这里可以考虑利用组合或继承进行实现。

- 管理对象（任务）：任务结构实际上也会根据OS的情况存在具体差异，但是对于设计模拟器来说是基本不变的，因此，这里不做过多抽象隔离设计。

总之，如图3所示，其基本关系可以确定为：
- 具体任务管理方法与具体管理结构、管理对象对应，但依赖的是抽象。
- 抽象任务管理方法与抽象管理结构、管理对象对应。
- 外部调用抽象任务管理方法

<div align="center">
    <img src="img/taskmanagement_software_arch.jpg" alt="任务管理软件结构" width="100%"/>
    <div>图3 任务管理软件架构</div>
</div>

大致伪代码实现如下：

```
// sched.c/h
struct task_struct {
  int id;
  void (*task_function)(void);
  ...
};


struct scheduler {
  int timefd;
  struct task_struct *current_task;
  ...
};

struct sched_function {
  void (*create_task)(struct task_struct *task);
  void (*activate_task)(struct scheduler *sched);
  ...
};

scheduler *global_scheduler;

// table_scheduler.c/h

struct table_scheduler {
  struct scheduler sched;
  int duration;
  struct EntryPoint ep[MAX_NUM_EP];
  ...
};

struct table_scheduler scheduler;

void regist_scheduler()
{
  global_scheduler = &scheduler.sched;
}

void table_activate_task(struct scheduler *sched) {
  table_scheduler *table_sched = ‌container_of(sched, struct scheduler, sched);
  ...
  table_activate(table_sched);
}

struct sched_function table_sched_function = {
  .activate_task = table_activate_task,
  .create_task = NULL,
  ...
}

```