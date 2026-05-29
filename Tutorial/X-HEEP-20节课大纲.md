# X-HEEP 20 节深度学习课程大纲

## 课程概述

本课程围绕开源 RISC-V MCU **X-HEEP** 展开，覆盖从环境搭建、软件编程、硬件架构、总线协议、自定义加速器到 FPGA/ASIC 的完整链路。共 20 节课，循序渐进，适合有数字电路和 C 语言基础的学习者。每节课 2-4 学时（含课后任务），总计 60-80 学时。

项目地址：<https://github.com/esl-epfl/x-heep>
官方文档：<https://x-heep.readthedocs.io/en/latest/>

---

## 模块一：入门与环境搭建（第 1-3 课）

---

### 第 1 课：课程总览与项目背景

**目标**：建立全局认知，了解课程规划。

- X-HEEP 简介：可配置、可扩展的异构 RISC-V MCU
- 技术渊源：PULP-Platform（ETHZ/UniBO）+ OpenTitan（lowRISC）+ OpenHW Group
- 核心价值：完整 MCU 平台 + 自由添加自定义加速器
- 课程路线图概览：七个模块、20 节课的递进逻辑
- 阅读 `docs/source/images/xheep_diagram.svg`：建立顶层框图第一印象
- 浏览项目 README，了解支持的仿真器、FPGA 板卡、ASIC 工艺

**课后任务**：浏览 [Read the Docs](https://x-heep.readthedocs.io/en/latest/) 的 Getting Started 和 Configuration 章节；加入 Matrix 社区 `#x-heep:matrix.org`。

---

### 第 2 课：环境搭建

**目标**：完成全部开发工具链的安装与验证。

- RISC-V GCC 工具链：`riscv32-unknown-elf-gcc`
- Verilator：开源 SystemVerilog 仿真器
- GTKWave：波形查看器
- FuseSoC：.core 文件驱动的硬件构建系统
- CMake + Ninja
- Python 依赖（`docs/python-requirements.txt`）
- 全工具链自检：逐一运行 `--version` 确认可用

**课后任务**：编译 `sw/applications/example_gpio_toggle`，确认生成 .elf/.bin/.hex 文件；用 `objdump -d` 反汇编查看 .text 段。

---

### 第 3 课：第一次仿真 — 让 X-HEEP 跑起来

**目标**：运行 Verilator 仿真，观察 MCU 工作。

- FuseSoC 命令详解：`fusesoc run --target=sim openhwgroup.org:systems:core-v-mini-mcu`
- 仿真全流程：RTL 编译 → 固件加载 → 仿真执行 → UART 日志输出
- GTKWave 基本操作：加载 .fst 波形、添加/删除信号、缩放时间轴、搜索信号
- 关键信号初识：`clk_i`、`rst_ni`、UART TX、GPIO 输出引脚
- Testbench 拓扑：`tb/tb_top.sv` 如何例化 DUT

**课后任务**：成功跑通仿真，在 GTKWave 中找到时钟和复位信号并观察其时序；修改 GPIO toggle 的打印内容后重新编译仿真。

---

## 模块二：软件视角（第 4-7 课）

---

### 第 4 课：固件结构 + CMake 构建系统

**目标**：理解固件的组织方式，能独立新增 application。

- `sw/` 目录全景：
  - `applications/` — 50+ 示例应用
  - `device/lib/` — HAL 驱动库（按外设分目录）
  - `device/target/` — 启动文件（crt0.S）、中断向量表
  - `linker/` — 链接脚本，定义内存布局
  - `freertos/` — FreeRTOS 移植
- CMake 构建深入：`sw/CMakeLists.txt` 顶层逻辑、工具链文件
- 编译选项含义：`-march=rv32imc`、`-mabi=ilp32`
- .elf → .bin → .hex → .vmem 的转换链路
- 实操：如何新增一个 application 并注册到构建系统

**课后任务**：手写一个 `hello_world` application；对比 .map 文件中 .text/.data/.bss/.stack 段的内存布局。

---

### 第 5 课：GPIO 驱动深度剖析

**目标**：建立"软件 → MMIO → 硬件寄存器"的完整认知链路。

- 以 `example_gpio_toggle` 为线索逐层追踪：
  ```
  main() → gpio_write() → mmio_region_write32() → 总线写事务 → GPIO RTL
  ```
- 寄存器代码生成机制：Hjson 描述 → `regtool.py` → SystemVerilog + C 头文件
- MMIO 的实现原理：基地址宏 + 偏移量 → 绝对地址 → load/store 指令
- GPIO 硬件 RTL（`hw/vendor/pulp_platform/gpio/`）输入输出通路

**课后任务**：不调用 HAL，直接用 `mmio_region_write32()` 宏操作 GPIO 寄存器实现闪烁；在 GTKWave 中观察一次 GPIO 写对应的 OBI 总线事务波形。

---

### 第 6 课：外设驱动族 — UART、I2C、SPI、Timer

**目标**：掌握常用外设的 HAL 编程模式。

- UART：波特率配置、TX/RX、中断 vs 轮询模式
- I2C：主机模式，参考 `example_i2c_tmp112`（温度传感器）
- SPI：主机/从机模式，参考 `example_spi_master_slave`
- Timer：OpenTitan RV Timer，配置定时中断
- 外设驱动共性总结：init 结构体 → init() → 读写 API → ISR 处理

**课后任务**：编写综合应用，每秒通过 UART 打印一次模拟传感器读数（信息通过不同外设交互展示）。

---

### 第 7 课：中断系统与异常处理

**目标**：理解中断从硬件信号到 ISR 函数执行的完整链路。

- X-HEEP 中断拓扑：外设 → PLIC → fast_intr_ctrl → CPU
- RISC-V Machine Mode 中断模型：
  - CSR 寄存器：`mtvec`、`mie`、`mip`、`mstatus`、`mcause`、`mepc`
  - 中断入口 → 保存上下文 → 执行 ISR → 恢复上下文 → `mret`
- PLIC 编程：中断使能、优先级、阈值、Claim/Complete 机制
- fast_intr_ctrl 的特殊功能（快速中断路由）
- 以 `example_gpio_intr` 为例讲解完整流程

**课后任务**：在 `example_gpio_intr` 基础上添加 UART 接收中断（收到字符后回显）；在 GTKWave 中观察从中断信号拉高到 CPU 进入 ISR 的时序延迟。

---

## 模块三：硬件架构（第 8-10 课）

---

### 第 8 课：顶层模块拆解

**目标**：读懂 MCU 顶层 RTL，理解子系统划分与接口。

- `core_v_mini_mcu.sv` 逐段精读：
  - 四个子系统的例化层次
  - 参数化设计：`#(parameter ...)` 如何影响硬件生成
  - 信号连接：地址线、数据线、中断线、控制线
- 各子系统精读：
  - `cpu_subsystem.sv` — CPU 核心 + XIF 协处理器接口
  - `memory_subsystem.sv` — RAM + Flash 控制器（obi_spimemio）+ 地址译码
  - `peripheral_subsystem.sv` — 外设总线拓扑
  - `ao_peripheral_subsystem.sv` — Always-On 域（电源/时钟管理）
- `debug_subsystem.sv` — JTAG 调试链路

**课后任务**：手绘 X-HEEP 模块连接图，标注地址总线和数据总线位宽；找到 CPU 指令总线与数据总线的地址范围（哪些地址是取指，哪些是访存）。

---

### 第 9 课：总线系统 — OBI 协议与 Crossbar

**目标**：完全理解片内互联机制。

- OBI（Open Bus Interface）协议深入：
  - 信号列表与含义：`req`/`gnt`、`rvalid`、`addr`、`wdata`、`rdata`、`we`、`be`
  - 读写事务时序图
  - 与 AXI、AHB 的对比：OBI 更精简，适合 MCU 级别
- Crossbar 实现详解：
  - `system_xbar.sv` — 多主多从互联拓扑
  - `xbar_varlat_one_to_n.sv` — 一对多地址路由
  - `xbar_varlat_n_to_one.sv` — 多对一仲裁逻辑
- 完整地址映射表：每个外设的基地址和地址范围
- `system_bus.sv`：OBI → AXI 协议桥（为什么需要 AXI？）

**课后任务**：在 GTKWave 中分别捕获一次 OBI 读事务和写事务，画时序图标注每个信号跳变时刻；对照 `system_xbar.sv` 理解地址路由源码。

---

### 第 10 课：IP 模块详解

**目标**：深入理解每个 IP 模块的硬件实现。

- `hw/ip/` 逐个讲解：
  - `soc_ctrl` — 时钟门控、复位分发、系统信息寄存器
  - `dma_subsystem` — 多通道 DMA 架构、传输模式（1D/2D）
  - `fast_intr_ctrl` — 中断路由与快速响应
  - `power_manager` — 电源域、休眠/唤醒状态机
  - `pdm2pcm` — 音频数据通路（抽取滤波器）
  - `obi_fifo` — 总线 FIFO（跨时钟域、反压处理）
  - `boot_rom` — 启动代码与启动流程
- `peripheral_subsystem` vs `ao_peripheral_subsystem`：时钟域和复位域差异
- `hw/ip_examples/`：加速器参考实现概览

**课后任务**：阅读 `hw/ip/power_manager/` 的 RTL，画出电源状态转换图；阅读至少一个 `hw/ip_examples/` 中的示例。

---

## 模块四：RISC-V CPU 与调试（第 11-13 课）

---

### 第 11 课：RISC-V 指令集 — RV32IMC 精要

**目标**：掌握 X-HEEP 所用指令集，能读懂反汇编。

- RV32I 整数指令集：
  - 算术/逻辑：`add`、`sub`、`andi`、`ori`、`xori`、`slli`、`srli`、`srai`
  - Load/Store：`lw`、`sw`、`lh`、`lb`、`lbu`、`sh`、`sb`
  - 分支/跳转：`beq`、`bne`、`blt`、`bge`、`bltu`、`bgeu`、`jal`、`jalr`
  - CSR 操作：`csrrw`、`csrrs`、`csrrc`、`csrrwi`
- RV32M 乘除法：`mul`、`mulh`、`mulhsu`、`div`、`divu`、`rem`、`remu`
- RV32C 压缩指令：16 位 vs 32 位编码，代码密度收益
- RISC-V 调用约定（ABI）：`a0-a7`、`t0-t6`、`s0-s11`、`sp`、`ra`、`gp`、`tp`
- 实操：`objdump -d` 反汇编，逐条分析指令

**课后任务**：反汇编 `example_matmul`，找出乘法和 load/store 指令；参考 `example_asm` 手写一段汇编实现 32-bit 整数数组求和。

---

### 第 12 课：CV32E40P 微架构

**目标**：理解 CPU 内部结构，能追踪单条指令的硬件执行过程。

- 流水线：IF（取指）→ ID（译码）→ EX（执行）→ WB（写回）
- 指令总线接口：取指事务特点（顺序、只读）
- 数据总线接口：Load/Store 事务
- CSR 寄存器详解：
  - 机器信息：`misa`、`mvendorid`、`marchid`
  - 状态控制：`mstatus`（MIE、MPIE、MPP）
  - 陷阱相关：`mtvec`、`mie`、`mip`、`mcause`、`mepc`、`mtval`
  - 性能计数器：`mcycle`、`minstret`
- 硬件乘法器的实现与多周期延迟
- 对比 CVE2（2 级流水线）的面积与性能差异

**课后任务**：在 GTKWave 中追踪一条 `lw t0, 0(sp)` 指令从取指到写回的完整波形；阅读 `hw/vendor/openhwgroup/cv32e40p/` 中 IF 阶段的 RTL 代码。

---

### 第 13 课：调试子系统 — JTAG 与 RISC-V Debug

**目标**：理解硬件调试的工作原理。

- RISC-V Debug Specification 0.13 概述
- `debug_subsystem.sv` 结构：
  - JTAG TAP 控制器（状态机）
  - DMI（Debug Module Interface）：JTAG ↔ Debug Module 的桥梁
  - Debug Module：halt、resume、abstract command、program buffer
- GDB 远程调试链路：GDB → OpenOCD → remote-bitbang → JTAG → Debug Module → CPU
- 硬件断点（trigger module）vs 软件断点（`ebreak`）的机制差异
- 单步执行的硬件实现：`dcsr.step` 位

**课后任务**：阅读 `hw/vendor/pulp_platform/riscv_dbg/` 中 Debug Module 的 halt/resume 状态机源码；思考 GDB 执行 `continue` 时硬件上发生了什么。

---

## 模块五：配置与代码生成（第 14-15 课）

---

### 第 14 课：配置驱动硬件生成

**目标**：理解 Hjson 配置如何生成不同的硬件实例。

- 配置体系总览：Hjson 配置 → Python 处理 → FuseSoC 参数 → RTL `generate`/宏
- Hjson 配置精读：
  - `configs/minimal.hjson`：最小系统（仅 CPU + 基本外设）
  - `configs/general.hjson`：全功能配置（所有外设）
  - 配置项：CPU 类型、外设使能/数量、地址空间、中断分配
- `.sv.tpl` 模板文件：Mako 模板引擎语法（条件例化、循环生成、变量替换）
- 以 `peripheral_subsystem.sv.tpl` 为例看条件例化外设
- `util/xheep_gen/` 生成脚本的作用
- 实践：创建自定义 Hjson 配置

**课后任务**：创建 `my_config.hjson`，关闭 I2C 和 SPI，仅保留 UART+GPIO+Timer；用此配置仿真，对比 RTL 编译日志确认外设不再例化。

---

### 第 15 课：寄存器代码生成 — regtool.py

**目标**：理解寄存器描述文件到 RTL/C 代码的自动生成流程。

- PULP Register Interface 设计思想
- regtool.py（`hw/vendor/pulp_platform/register_interface/vendor/lowrisc_opentitan/util/regtool.py`）：
  - 输入：Hjson 寄存器描述
  - 输出：SystemVerilog 寄存器模块 + C 头文件 + 文档（可选）
  - 支持特性：字段级访问控制（RW/RO/WO/RW1C）、硬件写使能、中断寄存器、寄存器数组
- Hjson 描述格式详解：`name`、`desc`、`swaccess`、`hwaccess`、`resval`、`fields`
- 以 GPIO 为案例看完整链路：描述文件 → 生成的 RTL → 生成的 C 头文件
- 实操：为一个简单的 PWM 外设编写寄存器描述，运行 regtool.py

**课后任务**：编写 PWM 外设的寄存器描述（含 CTRL、PRESCALER、DUTY 寄存器），运行 regtool.py 生成 RTL 和 C 头文件。

---

## 模块六：自定义加速器 ★ 核心（第 16-18 课）

---

### 第 16 课：加速器集成模式

**目标**：掌握 OBI 总线挂载和 XIF 协处理器两种方式的原理与选型。

- **模式一：OBI 总线挂载**
  - 加速器作为 OBI Slave，CPU 通过 MMIO 访问
  - 地址空间分配、总线时序对齐
  - 适用：独立数据处理、需要 DMA、IO 密集型
  - 优点：统一接口、跨 CPU 兼容
  - 缺点：访问延迟（需经过总线仲裁和译码）
- **模式二：XIF 协处理器**
  - CV32E40X 的 eXternal Interface
  - 自定义指令触发、offload 信号协议
  - 适用：紧耦合计算、低延迟、流式处理
  - 优点：低延迟（旁路总线）、可定义自定义指令
  - 缺点：依赖 XIF 支持的 CPU、接口更复杂
- 选型决策矩阵：延迟 vs 吞吐 vs 复杂度 vs CPU 兼容性
- 阅读 `hw/ip_examples/` 中的 OBI 示例和 `cv32e40px_xif_wrapper.sv`

**课后任务**：分析 `hw/ip_examples/` 中至少一个完整示例的 RTL 和驱动代码；阅读 `cv32e40px_xif_wrapper.sv` 理解 XIF 信号连接；输出两种集成方式的对比表。

---

### 第 17 课：加速器 RTL 设计

**目标**：用 SystemVerilog 完成加速器模块级设计与仿真验证。

- 需求规格：4×4 矩阵乘法（16-bit 输入，32-bit 输出），寄存器写入操作数、启动计算、读取结果
- RTL 架构设计：
  - OBI Slave 接口模块（req/gnt/rvalid 握手时序）
  - 寄存器文件（地址译码 + 字段级读写控制）
  - MAC 计算阵列数据通路（面积 vs 吞吐权衡）
  - 控制状态机：IDLE → LOAD_A → LOAD_B → COMPUTE → STORE → DONE
- 关键设计决策：
  - MAC 单元数量：1 个（最小面积）vs 4 个（行并行）vs 16 个（全并行）
  - 中断 vs 轮询完成检测
  - 是否需要双缓冲支持乒乓操作
- 独立模块级仿真：编写 testbench，验证计算正确性

**课后任务**：完成矩阵乘法加速器 RTL 设计与模块级仿真，验证多种测试矩阵的正确性。

---

### 第 18 课：加速器全流程集成

**目标**：将加速器集成到 X-HEEP，完整经历 RTL→驱动→应用的端到端流程。

- Step 1 — 注册到配置系统：
  - 在 Hjson 中添加 IP 描述
  - 分配地址空间（不与已有外设冲突）
  - 分配中断线（若使用中断）
- Step 2 — 集成到顶层 RTL：
  - 在 `peripheral_subsystem.sv.tpl` 中添加条件例化
  - 连接 OBI Slave 接口到系统总线
  - 连接中断到 PLIC
- Step 3 — 编写 HAL 驱动：
  - 在 `sw/device/lib/` 中创建驱动目录
  - 实现 API：`init()`、`start()`、`is_done()`、`read_result()`
  - 注册到 CMake 构建系统
- Step 4 — 编写验证应用：
  - 创建 application，调用加速器
  - 对比硬件加速 vs 纯软件矩阵乘法的正确性和性能
  - 输出加速比（周期数对比）

**课后任务**：完成全部集成步骤，在 Verilator 仿真中跑通硬件加速矩阵乘法；输出硬件加速 vs 软件的耗时对比和加速比数据。

---

## 模块七：从仿真到芯片（第 19-20 课）

---

### 第 19 课：FPGA 原型验证 + ASIC 物理实现

**目标**：理解 RTL 如何变成真正的芯片。

- FPGA 实现流程：
  - Vivado 项目创建、引脚约束（.xdc 文件）
  - 综合 → 布局布线 → 比特流生成 → 下载
  - 支持板卡：Nexys-A7、Pynq-Z2、ZCU104
  - FPGA 顶层 wrapper（`hw/fpga/`）
- ASIC 实现流程（SkyWater 130nm 开源工艺）：
  - sv2v：SystemVerilog → Verilog 转换（Yosys 不支持 SV）
  - Yosys 逻辑综合
  - OpenROAD 布局布线
  - OpenSTA 静态时序分析
  - 参考 `hw/asic/sky130/` 和 `flow/OpenROAD-flow-scripts/`
- 关键指标解读：Fmax（最大频率）、Cell Area（面积）、Power（功耗）
- 对比：FPGA 原型验证 vs ASIC 流片在流程和指标上的差异

**课后任务**：跑通 `asic_yosys_synthesis` 目标，阅读综合报告；如有 FPGA 板，在实板上跑通 GPIO toggle。

---

### 第 20 课：总结与进阶方向

**目标**：梳理完整知识体系，指明后续深入方向。

- 20 节课知识体系回顾：
  ```
  环境搭建 → 固件驱动 → 硬件架构 → 总线协议
      ↓
  CPU 微架构 → 配置系统 → 自定义加速器 → FPGA/ASIC
  ```
- X-HEEP 进阶方向：
  - **安全**：OpenTitan 加密加速器、安全启动（secure boot）
  - **AI 加速**：TinyML 神经网络推理引擎
  - **异构计算**：多核 RISC-V + 硬件加速器阵列
  - **电源优化**：DVFS（动态调频调压）、多电源域策略
  - **验证进阶**：UVM 验证、形式化验证（riscv-formal）
- 如何为 X-HEEP 贡献代码：
  - GitHub Issue/PR 流程
  - Solderpad Hardware License (SHL-2.1) 合规
  - Matrix 社区 `#x-heep:matrix.org`
- 推荐资源汇总：论文、书籍、课程

**课后任务（结课项目）**：独立设计一个自定义加速器（FIR 滤波器 / CRC 计算器 / AES 引擎 / 图像卷积），完整经历 RTL 设计 → 集成 → 驱动 → 应用 → 仿真验证 → 性能分析的全流程，撰写项目报告。

---

## 课程路线图

```
模块一（1-3课）        模块二（4-7课）        模块三（8-10课）
┌──────────────┐      ┌──────────────┐      ┌──────────────┐
│ 入门与环境搭建  │      │ 软件视角       │      │ 硬件架构       │
│              │      │              │      │              │
│ 1. 课程总览   │ ───→ │ 4. 固件+CMake │ ───→ │ 8. 顶层拆解    │
│ 2. 工具链安装  │      │ 5. GPIO 剖析  │      │ 9. OBI+Crossbar│
│ 3. 第一次仿真  │      │ 6. 外设驱动族  │      │ 10. IP 详解    │
│              │      │ 7. 中断系统    │      │              │
└──────────────┘      └──────────────┘      └──────────────┘
       │                      │                      │
       ▼                      ▼                      ▼
模块四（11-13课）      模块五（14-15课）      模块六（16-18课）★
┌──────────────┐      ┌──────────────┐      ┌──────────────┐
│ RISC-V 深入   │      │ 配置与代码生成  │      │ 自定义加速器   │
│              │      │              │      │              │
│ 11. RV32IMC  │ ───→ │ 14. Hjson配置 │ ───→ │ 16. 集成模式   │
│ 12. CV32E40P  │      │ 15. regtool   │      │ 17. 加速器RTL  │
│ 13. JTAG 调试 │      │              │      │ 18. 全流程集成  │
└──────────────┘      └──────────────┘      └──────────────┘
                                                    │
                                                    ▼
                    模块七（19-20课）
              ┌──────────────────┐
              │ 从仿真到芯片       │
              │                  │
              │ 19. FPGA + ASIC  │
              │ 20. 总结与进阶    │
              └──────────────────┘
```

---

## 学习进度自检

### 模块一 ✓
- [ ] 全部工具链安装可用（GCC、Verilator、GTKWave、FuseSoC）
- [ ] 成功编译固件应用，理解 objdump 反汇编
- [ ] 成功运行 Verilator 仿真并查看波形

### 模块二 ✓
- [ ] 能独立创建新 application 并加入 CMake 构建
- [ ] 理解 HAL API → MMIO → 硬件寄存器的调用链
- [ ] 能编写 UART/I2C/SPI/Timer 操作程序
- [ ] 理解中断从硬件信号到 ISR 的完整流程

### 模块三 ✓
- [ ] 能画出 X-HEEP 模块连接图（含总线拓扑）
- [ ] 理解 OBI 读写时序，能在波形中识别
- [ ] 理解 Crossbar 地址路由与仲裁逻辑

### 模块四 ✓
- [ ] 能读懂反汇编，理解基本 RV32IMC 指令
- [ ] 能在波形中追踪单条指令的完整执行
- [ ] 理解 CSR 寄存器作用和 JTAG 调试链路

### 模块五 ✓
- [ ] 能创建自定义 Hjson 配置，控制外设使能
- [ ] 能用 regtool.py 生成新外设的寄存器代码

### 模块六 ✓
- [ ] 完成加速器 RTL 设计与模块级仿真
- [ ] 成功集成加速器到 X-HEEP
- [ ] 编写驱动和应用，在仿真中验证功能正确性

### 模块七 ✓
- [ ] 了解 FPGA 和 ASIC 实现流程
- [ ] 完成结课综合项目

---

## 推荐资源汇总

| 类别 | 资源 | 说明 |
|------|------|------|
| RISC-V | [The RISC-V Reader](http://riscvbook.com/) | 最佳入门书 |
| RISC-V | [riscv.org/specifications](https://riscv.org/) | 官方规范 |
| SystemVerilog | "SystemVerilog for Design" — Sutherland | RTL 设计参考 |
| SystemVerilog | [HDLBits](https://hdlbits.01xz.net/) | 在线练习 |
| X-HEEP | [Read the Docs](https://x-heep.readthedocs.io/) | 项目官方文档 |
| X-HEEP | `hw/ip_examples/` | 加速器参考示例 |
| X-HEEP | ISVLSI 2025 论文 | 项目学术引用 |
| PULP | [PULP-Platform](https://github.com/pulp-platform) | 总线、调试、寄存器接口 |
| OpenTitan | [opentitan.org](https://opentitan.org/) | 外设 IP 来源 |
| OpenHW | [openhwgroup.org](https://www.openhwgroup.org/) | CPU 核心（CV32E40P/X） |

---

> **建议**：硬件背景从模块三、四、六主攻；软件背景从模块二切入再补硬件。每节课 2-4 小时，**模块六（自定义加速器）** 是课程核心，建议分配最多时间。
