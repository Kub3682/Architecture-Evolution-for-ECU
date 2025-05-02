# 📦 AUTOSAR Classic Platform Task Schedule Simulator

> 面向AUTOSAR Classic平台的任务调度模拟器

---

## 🧭 项目简介

AUTOSAR Classic Platform Task Schedule Simulator 是一个用于模拟 AUTOSAR Classic 平台中 任务调度行为 的轻量级仿真系统。该工具帮助开发者和研究人员在无需真实 ECU 或复杂 RTOS 的情况下，准确分析、验证任务的调度过程、响应时间、优先级冲突和系统负载等关键指标。

* 本模拟器特别适用于以下场景：

* 任务调度策略研究与验证（如 OSEK 基于优先级的抢占式调度）

* AUTOSAR 软件组件（SWC）行为分析

* 早期系统架构评估与优化

* 教学与培训用途

---

## ✨ 项目特性（Features）

- ✅ 支持周期任务与单发任务：兼容 AUTOSAR Classic Platform 中常见任务模型

- 📅 基于表的任务调度机制：实现时间触发型（Time-Triggered）任务调度

- 🧩 支持多调度表扩展：允许配置和切换多个调度表，适应不同运行场景

- 🔍 可视化任务调度过程（如使用 Gantt 图/时序图，如已实现）

- 🧪 完善的单元测试覆盖：设计并实现关键调度逻辑的测试用例，确保调度行为一致性

- 🧰 可插拔的调度策略接口：支持未来扩展调度策略（如抢占式、优先级混合型等）

- 📖 对 AUTOSAR 调度语义保持一致性：符合标准描述的调度行为和约束

- 🎯 轻量、易于集成和修改：适用于研究、教学、概念验证和早期架构分析

---

## 🛠️ 技术栈（Tech Stack）

- **语言**：c99
- **构建系统**：Make
- **文档工具**：MkDocs（Markdown 支持）、Doxygen
- **测试框架**：GoogleTest 
- **可视化**：Matplotlib

---

## 🚀 快速上手（Quick Start）

### 1. 克隆仓库

```bash
git clone https://github.com/yourname/MatrixDecompX.git
cd MatrixDecompX
```

### 2. 编译项目


### 3. 运行测试

## 📚 查看详细文档
### 在线阅读文档：[https://yourname.github.io/YourProjectName/](https://yourname.github.io/YourProjectName/)
### 或直接查看：[`docs/index.md`](docs/index.md)