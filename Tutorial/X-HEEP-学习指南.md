# X-HEEP 深度学习指南

## 项目概览

X-HEEP（eXtensible Heterogeneous Energy-Efficient Platform）是一个基于 **RISC-V** 的**可配置、可扩展异构 MCU**，用 SystemVerilog 描述。

- **维护方**：EPFL ESL 实验室、UPM CEI、POLITO VLSI 实验室联合维护
- **技术基础**：ETHZ/UniBO 的 PULP-Platform + lowRISC 的 OpenTitan
- **CPU 核心**：CV32E40P / CV32E40X / CVE2（OpenHW Group）
- **核心价值**：提供一个完整的、工业级验证的 MCU 平台（外设、DMA、中断、总线），你可以**专注于添加自己的硬件加速器**，而不需要从零开始搭建 MCU
- **支持技术栈**：
  - 仿真：Verilator、Questasim、VCS、Xcelium
  - FPGA：Nexys-A7、Pynq-Z2、ZCU104/102 等
  - ASIC：SkyWater 130nm（OpenROAD）
  - 软件：baremetal / FreeRTOS，CMake + GCC/Clang

**文档**：[x-heep.readthedocs.io](https://x-heep.readthedocs.io/en/latest/index.html)
**社区**：Matrix `#x-heep:matrix.org`

---

## 第一阶段：理解整体架构（1-2 周）

> 目标：搞清楚“这个芯片里有什么”

### 1.1 阅读顶层架构图

- 查看 `docs/source/images/xheep_diagram.svg`，理解：
  - CPU 子系统如何连接
  - 总线（OBI / AXI）拓扑
  - 外设和存储器的挂载位置
  - 中断路由
  - 时钟域划分

### 1.2 阅读顶层 SystemVerilog 文件（按顺序）

| 文件 | 作用 |
|------|------|
| `hw/core-v-mini-mcu/core_v_mini_mcu.sv` | MCU 顶层模块 |
| `hw/core-v-mini-mcu/cpu_subsystem.sv` | CPU 子系统（CV32E40P/X 及其 XIF 接口） |
| `hw/core-v-mini-mcu/memory_subsystem.sv` | 存储器子系统（RAM、Flash 控制器） |
| `hw/core-v-mini-mcu/peripheral_subsystem.sv` | 外设子系统 |
| `hw/core-v-mini-mcu/ao_peripheral_subsystem.sv` | Always-On 外设（电源管理等） |
| `hw/core-v-mini-mcu/system_bus.sv` | 系统总线 |
| `hw/core-v-mini-mcu/system_xbar.sv` | 总线交叉开关（crossbar） |
| `hw/core-v-mini-mcu/debug_subsystem.sv` | JTAG 调试子系统 |

### 1.3 了解 CPU 核心

- **CV32E40P**：4 级流水线，RV32IMC，来自 OpenHW Group
- **CV32E40X**：CV32E40P 的变体，支持 XIF（eXternal Interface）可外接协处理器
- **CVE2**：2 级流水线，更小面积，也支持 XIF
- 参考：[OpenHW Group CV32E40P 用户手册](https://docs.openhwgroup.org/projects/cv32e40p-user-manual/)

### 1.4 浏览所有 IP 模块（`hw/ip/`）

```
hw/ip/
├── boot_rom          — 启动 ROM
├── dma_subsystem     — DMA 子系统
├── fast_intr_ctrl    — 快速中断控制器
├── obi_fifo          — OBI 总线 FIFO
├── obi_spimemio      — 通过 SPI 访问外部 Flash
├── pdm2pcm           — PDM 转 PCM 音频接口
├── power_manager     — 电源管理单元
├── serial_link_xheep_wrapper — 串行通信链路
└── soc_ctrl          — SoC 控制（时钟、复位、系统信息）
```

### 第一阶段思考题

1. CPU 通过什么总线协议访问外设？（答：OBI = Open Bus Interface）
2. 中断从外设到 CPU 的路由是怎样的？（PLIC → fast_intr_ctrl → CPU）
3. 系统有几个时钟域？每个时钟域包含哪些模块？
4. 复位信号如何分发到各个子系统？

---

## 第二阶段：从软件视角理解硬件（1-2 周）

> 目标：理解“软件怎么跑在这个芯片上”

### 2.1 软件目录结构

```
sw/
├── applications/     — 50+ 示例应用程序
├── device/
│   ├── bsp/          — 板级支持包
│   ├── lib/          — HAL 驱动库（GPIO、UART、I2C、SPI、DMA...）
│   └── target/       — 目标平台相关代码（启动文件、链接脚本）
├── freertos/         — FreeRTOS 移植及配置
├── linker/           — 链接脚本，定义内存布局
├── vendor/           — 第三方 SDK（如 OpenTitan）
├── cmake/            — CMake 工具链文件
└── CMakeLists.txt    — 顶层构建文件
```

### 2.2 跟读一个最简单的应用

以 `sw/applications/example_gpio_toggle/` 为例：

1. 从 `main()` 函数开始，看它如何调用 HAL API
2. 追踪 HAL 函数到寄存器级别的操作（`device/lib/` 中的驱动源代码）
3. 理解外设寄存器地址映射（内存映射 IO = MMIO）
4. 看链接脚本理解代码和数据如何放置到 RAM/Flash 中

### 2.3 理解构建系统

- 顶层 `sw/CMakeLists.txt`：定义了所有应用和库的构建规则
- `sw/cmake/`：工具链文件（GCC / Clang）
- 学习命令：`cmake -B build && cmake --build build`

### 2.4 理解外设寄存器模型

- 寄存器由 `regtool.py` 从描述文件自动生成 SystemVerilog 和 C 头文件
- 位于 `hw/vendor/pulp_platform/register_interface/`
- 类比理解：Hjson/JSON 描述 → 工具生成 → RTL 寄存器模块 + C 头文件

### 第二阶段思考题

1. GPIO 的寄存器基地址在哪里定义？
2. `gpio_write()` 到硬件引脚电平变化，经历了哪些步骤？
3. 如果想让程序从 Flash 启动，链接脚本需要怎么改？

---

## 第三阶段：动手跑仿真（1 周）

> 目标：把系统跑起来，看到波形

### 3.1 安装仿真环境

- **推荐入门**：Verilator（开源，无需 license）
- 安装方法参考 `docs/source/GettingStarted/`

### 3.2 跑通第一个仿真

```bash
# 使用 FuseSoC 构建和仿真
fusesoc --cores-root . run --target=sim openhwgroup.org:systems:core-v-mini-mcu

# 或用 Makefile
make verilator-sim
```

### 3.3 运行一个软件并观察波形

1. 编译一个 example（如 `example_gpio_toggle`）
2. 将生成的 .hex/.bin 加载到仿真中
3. 使用 GTKWave 查看波形：
   - CPU 取指/执行
   - 总线读写事务
   - GPIO 引脚翻转

### 3.4 尝试不同配置

X-HEEP 使用 Hjson 配置文件来定制硬件：

```
configs/
├── minimal.hjson     — 最小配置
├── general.hjson     — 通用配置（含大多数外设）
├── benchmark.hjson   — 性能基准测试配置
└── ci.hjson          — CI 测试配置
```

尝试切换不同配置，观察生成的硬件实例有何不同。

### 第三阶段思考题

1. Verilator 仿真 1 秒需要多少 wall-clock 时间？
2. 如何通过 `--trace` 选项控制波形的详细程度？
3. 如果只改配置不写代码，能得到哪些不同的硬件变体？

---

## 第四阶段：深入 RISC-V 与总线协议（2-3 周）

> 目标：理解 CPU 微架构和片内互联

### 4.1 RISC-V 指令集基础

- **必须掌握**：RV32I（整数基础）、RV32M（乘法）、RV32C（压缩指令）
- **推荐资源**：
  - "The RISC-V Reader" — 短小精悍（~200 页）
  - [riscv.org/specifications](https://riscv.org/specifications/) — 官方规范
- **在 X-HEEP 中的体现**：`hw/vendor/openhwgroup/` 中的 CV32E40P RTL 实现

### 4.2 CV32E40P 微架构

- 2 级流水线（取指 → 执行）
- 独立的指令总线和数据总线（Harvard 架构）
- 支持 RISC-V 特权模式（Machine Mode）
- 中断/异常处理机制

### 4.3 OBI（Open Bus Interface）协议

- X-HEEP 使用的片内总线协议（来自 PULP-Platform）
- 理解 OBI 的信号：
  - `req` / `gnt` — 请求 / 授予
  - `addr` / `wdata` / `rdata`
  - `we` — 读写控制
  - `be` — 字节使能
- 对比 AXI：OBI 更简单，适合 MCU 级别的互联

### 4.4 AXI 桥接

- 阅读 `hw/core-v-mini-mcu/system_bus.sv`
- 理解 OBI → AXI 的协议转换
- 为什么需要 AXI？（高速、支持 burst、业界标准、方便接外部 IP）

### 4.5 调试子系统

- `hw/core-v-mini-mcu/debug_subsystem.sv`
- 基于 RISC-V Debug Specification
- JTAG 协议 + 远程 bitbang 实现
- 理解 GDB 如何通过 JTAG 调试 MCU

### 第四阶段思考题

1. CPU 执行 `lw t0, 0x1000(t1)` 这条指令时，OBI 总线上出现什么信号？
2. 如果两个 Master 同时请求访问同一个 Slave，crossbar 如何仲裁？
3. XIF 接口在什么场景下会比通过总线挂加速器更优？

---

## 第五阶段：添加自定义加速器（核心亮点，2-4 周）

> 这是 X-HEEP 最核心的价值所在

### 5.1 理解加速器集成方式

X-HEEP 提供了两种集成加速器的方式：

| 方式 | 接口 | 适用场景 |
|------|------|----------|
| 总线挂载（OBI） | 通过 system_xbar 挂在总线上 | 通用外设/加速器，需 DMA |
| XIF 协处理器 | CV32E40X 的 eXternal Interface | 紧耦合计算加速，低延迟 |

### 5.2 阅读加速器示例（`hw/ip_examples/`）

`hw/ip_examples/` 中提供了参考示例，先读懂一个简单的：

1. 看 SystemVerilog 实现
2. 看如何注册到配置文件中
3. 看对应的 C 驱动
4. 看对应的 application 如何使用

### 5.3 动手：设计一个硬件矩阵乘法加速器

**Step 1 — RTL 设计：**
- 用 SystemVerilog 写加速器 IP
- 实现 OBI slave 接口
- 内部包含一个小型 MAC 阵列

**Step 2 — 挂接到总线：**
- 在配置文件中注册新 IP
- 分配地址空间
- 连接中断（如需要）

**Step 3 — 编写驱动：**
- 在 `sw/device/lib/` 中添加 HAL 驱动
- 提供初始化、配置、启动、查询等 API

**Step 4 — 编写应用：**
- 在 `sw/applications/` 中添加新应用
- 调用你的加速器执行矩阵乘法
- 对比纯软件的 FFT/matmul 性能

### 5.4 使用配置工具

- `configs/*.py` 和 `*.hjson`：定义硬件配置
- `util/xheep_gen/`：硬件生成脚本
- 理解如何通过配置文件实例化（或不实例化）某个 IP

### 第五阶段思考题

1. 你的加速器从 CPU 写入配置到产生结果，延迟是多少个周期？
2. 如果不通过总线，改用 XIF 紧密耦合，设计会有什么不同？
3. 加速器的中断如何通知 CPU 数据处理完成？

---

## 第六阶段：FPGA / ASIC 实现（选学，2-4 周）

### 6.1 FPGA 原型验证

- **支持板卡**：
  - Digilent Nexys-A7-100T
  - TUL Pynq-Z2
  - Xilinx ZCU104 / ZCU102
  - Genesys2
- **FPGA 相关文件**：`hw/fpga/`
- **构建命令**：
  ```bash
  fusesoc --cores-root . run --target=nexys-a7-100t openhwgroup.org:systems:core-v-mini-mcu
  ```

### 6.2 ASIC 流程

- **参考流程**：`hw/asic/sky130/`（SkyWater 130nm 开源工艺）
- **工具链**：OpenROAD-flow-scripts
- **关键步骤**：
  - 逻辑综合（Yosys + sv2v）
  - 布局布线（OpenROAD）
  - 时序分析（OpenSTA）
  - 功耗分析

### 6.3 功耗分析

- 阅读 `hw/ip/power_manager/` 理解电源域划分
- UPF（Unified Power Format）支持
- Always-On 域 vs 可关断域

---

## 学习路径全景图

```
┌─────────────────────────────────────────────────────────────┐
│  第一阶段（1-2周）      第二阶段（1-2周）     第三阶段（1周）   │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐   │
│  │  整体架构     │ → │  软件视角     │ → │  动手仿真      │   │
│  │  RTL 顶层     │    │  HAL/SDK      │    │  Verilator    │   │
│  │  IP 清单      │    │  构建系统     │    │  波形分析     │   │
│  └──────────────┘    └──────────────┘    └──────────────┘   │
│         ↓                   ↓                   ↓           │
│  第四阶段（2-3周）      第五阶段（2-4周）     第六阶段（选学）  │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐   │
│  │  RISC-V 深入  │ → │  自定义加速器 │ → │  FPGA/ASIC    │   │
│  │  总线协议     │    │  硬件+驱动+App │    │  芯片实现     │   │
│  │  微架构       │    │  配置系统     │    │  功耗分析     │   │
│  └──────────────┘    └──────────────┘    └──────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

---

## 推荐学习资源

### RISC-V

| 资源 | 说明 |
|------|------|
| [The RISC-V Reader](http://riscvbook.com/) | 最佳入门书，短小精悍 |
| [riscv.org/specifications](https://riscv.org/specifications/) | 官方规范 |
| [riscv-formal](https://github.com/YosysHQ/riscv-formal) | RISC-V 形式化验证 |

### SystemVerilog

| 资源 | 说明 |
|------|------|
| "SystemVerilog for Design" — Sutherland | 设计用 SV 最佳参考 |
| "SystemVerilog for Verification" — Spear | 验证用 SV |
| [HDLBits](https://hdlbits.01xz.net/) | 在线 SV 练习 |

### X-HEEP 本身

| 资源 | 说明 |
|------|------|
| [Read the Docs](https://x-heep.readthedocs.io/en/latest/) | 项目官方文档 |
| ISVLSI 2025 论文 | 项目学术论文（README 中有引用） |
| Matrix 频道 `#x-heep:matrix.org` | 社区交流 |
| `hw/ip_examples/` | 加速器参考示例 |
| `docs/source/` 下的 rst 文件 | GettingStarted、Configuration、Extending 等 |

### 相关开源项目

| 项目 | 与 X-HEEP 的关系 |
|------|-------------------|
| [PULP-Platform](https://github.com/pulp-platform) | 总线、寄存器接口、调试模块等 |
| [OpenTitan](https://opentitan.org/) | UART、SPI、I2C、Timer、PLIC 等外设 |
| [OpenHW Group](https://www.openhwgroup.org/) | CV32E40P/X CPU 核心 |
| [YosysHQ](https://github.com/YosysHQ) | sv2v（SystemVerilog 转 Verilog） |

---

## 学习进度自检清单

### 第一阶段 ✓
- [ ] 能画出 X-HEEP 的模块框图
- [ ] 知道核心 RTL 文件中每个文件的作用
- [ ] 能说出至少 5 个 IP 模块及其功能

### 第二阶段 ✓
- [ ] 能独立编译并运行一个示例应用
- [ ] 理解 HAL API 到寄存器的调用流程
- [ ] 理解链接脚本中各个段的含义

### 第三阶段 ✓
- [ ] 成功运行 Verilator 仿真
- [ ] 能在 GTKWave 中观察 OBI 总线波形
- [ ] 尝试过修改配置并重新仿真

### 第四阶段 ✓
- [ ] 理解 RV32I 的基本指令集
- [ ] 能分析一条 load 指令在总线上的时序
- [ ] 理解 OBI 和 AXI 的区别

### 第五阶段 ✓
- [ ] 成功添加过一个自己写的简单 IP
- [ ] 为该 IP 编写了 C 驱动
- [ ] 编写应用程序验证了硬件功能

### 第六阶段（选学）✓
- [ ] 在 FPGA 上成功运行 X-HEEP
- [ ] 跑通 ASIC 综合流程
- [ ] 理解电源域和功耗管理策略

---

> 建议：如果你是**偏硬件背景**（数字 IC / FPGA 工程师），从第一、四、五阶段深入；如果你是**偏软件背景**（嵌入式工程师），从第二、三阶段切入，再回头补硬件知识。不管哪种背景，**第五阶段（自定义加速器）** 是理解整个平台价值的关键，值得花最多时间。
