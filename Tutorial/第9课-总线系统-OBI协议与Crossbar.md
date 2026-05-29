# 第 9 课：总线系统 — OBI 协议与 Crossbar

## 学习目标

完成本课后，你应该能够：
- 说出 OBI 协议每个信号的名称、方向和含义
- 画出 OBI 读写事务的时序图
- 理解 system_xbar 的两种拓扑（NtoM vs onetoM）及其权衡
- 读懂地址解码规则，能根据地址判断一次访问路由到哪个从设备
- 理解 rr_arb_tree 的轮询仲裁机制
- 在 GTKWave 中追踪一次跨 xbar 的总线事务

---

## 1. 为什么需要总线

第 5 课讲了 CPU 通过 MMIO 写 GPIO 寄存器。但 CPU 的数据总线不是直接连到 GPIO 的——中间隔着一层**互联网络**。

X-HEEP 有 9 个 master（CPU 取指、CPU 访存、Debug、DMA×6）和 7 个 slave（RAM0、RAM1、AO 外设域、外设域、Flash、Debug、Error），不可能全互联——9×7=63 条直连线不现实。总线的作用就是**用一套共享通道 + 仲裁机制，让多个 master 有序地访问多个 slave**。

```
                    ┌──────────┐
CPU 取指 ──────────→│          │──→ RAM0
CPU 访存 ──────────→│  system  │──→ RAM1
Debug   ──────────→│  _xbar   │──→ AO 外设域
DMA×6   ──────────→│          │──→ 外设域
                    │          │──→ Flash
                    └──────────┘──→ Debug
```

---

## 2. OBI 协议深入

### 2.1 信号定义

OBI（Open Bus Interface）是 PULP-Platform 为 MCU 级别 SoC 设计的精简总线协议，只定义了 2 个结构体。

**请求通道**（master → slave，69 bit）：

```systemverilog
typedef struct packed {
    logic        req;       // 请求有效（1 = 我要访问）
    logic        we;        // 写使能（1 = 写，0 = 读）
    logic [3:0]  be;        // 字节使能（bit N 使能字节 N）
    logic [31:0] addr;      // 字节地址
    logic [31:0] wdata;     // 写数据
} obi_req_t;
```

**响应通道**（slave → master，34 bit）：

```systemverilog
typedef struct packed {
    logic        gnt;       // 授权（1 = slave 接受了请求）
    logic        rvalid;    // 读数据有效（1 = rdata 准备好了）
    logic [31:0] rdata;     // 读数据
} obi_resp_t;
```

| 信号 | 方向 | 含义 | 读事务 | 写事务 |
|------|------|------|--------|--------|
| `req` | M→S | 请求有效 | 1 | 1 |
| `we` | M→S | 写使能 | 0 | 1 |
| `be` | M→S | 字节使能 | 4'b1111 | 4'b1111（全字写） |
| `addr` | M→S | 目标地址 | ✓ | ✓ |
| `wdata` | M→S | 写入数据 | 无关 | ✓ |
| `gnt` | S→M | 授权接收 | ✓ | ✓ |
| `rvalid` | S→M | 读数据有效 | ✓ | 无关 |
| `rdata` | S→M | 读回数据 | ✓ | 无关 |

### 2.2 写事务时序

```
clk     ┌─┐  ┌─┐  ┌─┐  ┌─┐  ┌─┐
       ─┘ └──┘ └──┘ └──┘ └──┘ └──

req     ──────┐        ┌──────────
              └────────┘
addr    ──────┐ 0x30020180 ┌─────
              └────────────┘
wdata   ──────┐ 0x00000004 ┌─────
              └────────────┘
we      ──────┐        ┌──────────
              └────────┘
gnt     ──────────┐  ┌────────────
                  └──┘
```

时序要点：
- master 在时钟上升沿同时给出 `req`、`addr`、`wdata`、`we`、`be`
- slave 在同一拍或之后给出 `gnt = 1`，表示"我收下了"
- 如果 slave 忙，可以把 `gnt` 保持为 0，master 必须保持所有信号不变，直到 `gnt = 1`
- **无 rvalid 阶段**：写事务在 gnt 时就完成了

### 2.3 读事务时序

```
clk     ┌─┐  ┌─┐  ┌─┐  ┌─┐  ┌─┐  ┌─┐
       ─┘ └──┘ └──┘ └──┘ └──┘ └──┘ └──

req     ──────┐        ┌──────────
              └────────┘
addr    ──────┐ 0x30020180 ┌─────
              └────────────┘
we      ──────────────────────────（恒为 0）

gnt     ──────────┐  ┌────────────
                  └──┘

rvalid  ─────────────────┐  ┌─────
                         └──┘
rdata   ─────────────────┐ val ┌──
                         └─────┘
```

读事务分两段：
1. **请求段**：和写事务一样，master 给出 req + addr（we = 0）
2. **响应段**：slave 在准备好数据后拉高 `rvalid`，同时给出 `rdata`

读事务的延迟（从 gnt 到 rvalid）取决于 slave 的速度。SRAM 可能 1 拍，SPI Flash 可能几十拍。

### 2.4 OBI vs AXI — 为什么选 OBI

| 特性 | OBI | AXI4 |
|------|-----|------|
| 通道数 | 2（req + resp） | 5（写地址、写数据、写响应、读地址、读数据） |
| 乱序支持 | 不支持 | 支持（靠 ID 匹配） |
| Burst | 不支持 | 支持 |
| 同时读写 | 不支持（共享通道） | 支持（读写完全独立） |
| 典型面积 | < 5k 门 | > 30k 门 |
| 适用场景 | MCU（单核、低功耗） | 应用处理器（多核、DDR） |

OBI 的取舍非常明确：**放弃乱序和 burst，换取极小面积**。一个 MCU 不需要 DDR 带宽，省下的逻辑门更值钱。

### 2.5 OBI 的关键限制：无 outstanding 事务

OBI 没有事务 ID。这意味着每个 master 同一时刻只能有一个事务在飞行中——必须等前一个事务的响应回来，才能发下一个请求。

这个限制贯穿了 X-HEEP 的整个总线设计。后面会看到 xbar 如何通过 `valid_inflight_q` 这个单比特状态寄存器来强制串行化。

---

## 3. 地址映射

完整的地址映射定义在 `core_v_mini_mcu_pkg.sv` 中。

### 3.1 一级地址空间（system_xbar 的 7 个 slave）

| Slave | 起始地址 | 结束地址 | 大小 |
|-------|----------|----------|------|
| RAM0 | `0x00000000` | `0x00008000` | 32 KB |
| RAM1 | `0x00008000` | `0x00010000` | 32 KB |
| Debug | `0x10000000` | `0x10100000` | 1 MB |
| AO 外设域 | `0x20000000` | `0x20100000` | 1 MB |
| 外设域 | `0x30000000` | `0x30100000` | 1 MB |
| Flash | `0x40000000` | `0x41000000` | 16 MB |
| Error | `0xBADACCE5` | — | （故意无效） |

### 3.2 AO 外设域内部（0x20000000 段）

| 外设 | 基地址 | 功能 |
|------|--------|------|
| SOC_CTRL | `0x20000000` | 系统控制（时钟频率、启动选择） |
| BOOTROM | `0x20010000` | 启动 ROM（59×32bit 预编码指令） |
| SPI_FLASH | `0x20020000` | SPI Flash 控制器 |
| DMA | `0x20030000` | 多通道 DMA |
| POWER_MANAGER | `0x20040000` | 电源管理 |
| RV_TIMER_AO | `0x20050000` | AO 域 RV Timer |
| FAST_INTR_CTRL | `0x20060000` | 快速中断控制器 |
| PAD_CONTROL | `0x20080000` | 引脚复用控制 |
| GPIO_AO | `0x20090000` | AO 域 GPIO（引脚 0-7） |

### 3.3 外设域内部（0x30000000 段）

| 外设 | 基地址 | 功能 |
|------|--------|------|
| RV_PLIC | `0x30000000` | 平台级中断控制器 |
| SPI_HOST | `0x30010000` | SPI 主机 1 |
| GPIO | `0x30020000` | 外设域 GPIO（引脚 8-31） |
| I2C | `0x30030000` | I2C 主机 |
| RV_TIMER | `0x30040000` | 外设域 RV Timer |
| SPI2 | `0x30050000` | SPI 主机 2 |
| UART | `0x30080000` | UART |

每个外设占 64 KB 空间。64 KB 对于只有几十个寄存器的外设来说绰绰有余，多出来的地址空间未使用——写进去被忽略，读回来是 0。

### 3.4 外部地址空间

地址 ≥ `0xF0000000` 的事务被路由到外部总线端口，用于连接片外设备（FPGA 上的外部 SRAM 等）。

---

## 4. Crossbar 拓扑：NtoM vs onetoM

### 4.1 全互联 NtoM

```
Master 0 ──┬──┬──┬──┬──→ Slave 0
Master 1 ──┼──┼──┼──┼──→ Slave 1
Master 2 ──┼──┼──┼──┼──→ Slave 2
           │  │  │  │
        (9×7 arbiter + 9×7 multiplexer matrix)
```

每个 master 可以**同时**访问不同的 slave。M0 读 RAM0，M1 写 Flash，M2 读 GPIO——三件事并行进行。

代价：9×7=63 条内部通路，面积大。X-HEEP 默认不用这个。

### 4.2 两级 onetoM（X-HEEP 默认）

```
Master 0 ─┐
Master 1 ─┤
  ...     ├─→ [RR Arb] ─→ [Addr Decode] ─┬─→ Slave 0
Master 8 ─┘    N→1           1→N         ├─→ Slave 1
                                          └─→ ...
```

9 个 master 先通过**轮询仲裁**抢占一条共享通道，然后由**地址译码**决定去哪个 slave。

**优势**：面积小（9 个仲裁 + 7 路译码，vs 63 条通路）
**代价**：任何时候只有一个事务能通过瓶颈，其他 master 排队等

X-HEEP 默认用 onetoM 的原因：MCU 本来就是单核，大多数时候只有 CPU 在访问总线，DMA 偶尔插队，并行度要求不高，省面积更值。

### 4.3 xbar_varlat_n_to_one — 多对一瓶颈

这个模块是 onetoM 的第一级，把 N 个 master 的请求仲裁成一条。

关键设计：**outstanding 保护 FSM**。

```
状态 OUTSTANDING_REQ:  有 master 发出请求
状态 OUTSTANDING_WAITFOR_VALID:  该请求已被 gnt，等待 rvalid 返回
```

在 `OUTSTANDING_WAITFOR_VALID` 期间，即使有其他 master 想发请求，也会被阻塞——因为共享通道上还飞着一个未完成的事务。这保证了 OBI 的"无 outstanding 事务"约束。

```systemverilog
// xbar_varlat_n_to_one.sv 核心逻辑（简化）
always_ff @(posedge clk_i or negedge rst_ni) begin
    case (state_q)
        OUTSTANDING_REQ: begin
            if (slave_req_o.req && slave_resp_i.gnt)
                state_d = OUTSTANDING_WAITFOR_VALID;
        end
        OUTSTANDING_WAITFOR_VALID: begin
            if (slave_resp_i.rvalid)
                state_d = OUTSTANDING_REQ;
        end
    endcase
end
```

### 4.4 xbar_varlat_one_to_n — 一对多分发

onetoM 的第二级，把单条请求根据地址路由到正确的 slave。

内部就是例化一个 `addr_decode` 做地址匹配，然后把请求和响应连到对应的 slave 端口。

---

## 5. Round-Robin 仲裁（rr_arb_tree）

当多个 master 同时请求同一个 slave 时，需要仲裁。X-HEEP 用的是**对数级轮询仲裁树**。

### 5.1 轮询算法

```
请求轮到谁处理？

当前指针指向 master K → 从 K+1 开始找，先找到谁就服务谁
服务完后，指针移到 K+1

例：4 个 master，指针在 1
  请求：M0=0, M1=1, M2=1, M3=0
  查找：M2（从 M2 开始找起）→ 服务 M2
  下一轮指针移到 2
  请求：M0=1, M1=0, M2=0, M3=1
  查找：M3（从 M3 开始）→ M0（绕回）→ 服务 M0
```

### 5.2 树形结构

```
                   ┌────┐
                   │ RR │       Level 2（根节点）
                   └────┘
                  /      \
            ┌────┐        ┌────┐
            │ RR │        │ RR │  Level 1
            └────┘        └────┘
           /    \         /    \
        M0     M1      M2     M3   Level 0（叶子）

每个 RR 节点二选一。选出的请求逐级上传，最终只有一个 master 获得授权。
```

这种树形结构的好处是把 N 选 1 的复杂度从 O(N) 降到 O(log N)。

### 5.3 关键参数

- `LockIn`：当一个 master 被授权但 slave 还没 gnt 时，是否锁定仲裁结果。X-HEEP 设为 0（不锁）。
- `FairArb`：是否使用公平轮询算法（两个 LZC 找下一个未服务的请求）。默认开启。

---

## 6. system_bus — 顶层总线

`system_bus.sv` 在 system_xbar 外面又包了一层**主端口 demux**。

每个 master 前面有一个 1-to-2 demux：

```
Master → [1→2 Demux] ──→ 内部 system_xbar（地址 < 0xF0000000）
                      └─→ 外部端口（地址 ≥ 0xF0000000）
```

这是为了支持外部设备扩展。如果你在 FPGA 上外挂了一个 SRAM，映射到 `0xF0000000`，CPU 访问那块地址时事务就自动走到外部端口。

---

## 7. obi_fifo — 总线解耦缓冲

`obi_fifo` 是深度为 1 的微型 FIFO，插在生产者和消费者之间。

### 7.1 为什么需要它

在 FPGA 上，长走线会产生大的组合逻辑延迟。如果 master 的 `req` 到 slave 的 `gnt` 的组合路径太长，时序收敛不了。

obi_fifo 切断这条组合路径——master 的 req 先进 FIFO（一拍），下一拍再从 FIFO 出来给 slave。多了一拍延迟，但换来了干净得多的时序。

### 7.2 内部结构

```
producer_req ──→ [req FIFO (deep=1)] ──→ consumer_req
producer_resp ←── [resp FIFO (deep=1)] ←── consumer_resp
```

请求和响应各有独立的深度-1 FIFO，因此需要两套 FSM 配合工作。

生产者侧 FSM：在 WAIT_FOR_VALID 状态阻塞新的 gnt，直到消费端的 rvalid 返回。这保证了 OBI 的无 outstanding 约束不会因为 FIFO 延迟而被打破。

### 7.3 哪里用了 obi_fifo

AO 外设子系统和外设子系统的入口各有一个 obi_fifo（可通过 `REMOVE_OBI_FIFO` 宏去掉），用于隔离子系统内部的长组合逻辑路径。

---

## 8. 实践：在 GTKWave 中追踪总线事务

### 8.1 关键信号路径

**写 GPIO 的事务路径：**

```
CPU data bus → system_bus (per-master demux)
  → system_xbar (N→1 neck → 1→N fanout)
    → AO peripheral subsystem (obi_fifo)
      → GPIO reg_top
```

在 GTKWave 中可观察的信号：

| 观察点 | 路径 | 看什么 |
|--------|------|--------|
| xbar 入口 | `system_bus → system_xbar → master_req_i[1]` | CPU 发出的原始请求 |
| xbar 出口 | `AO peripheral → slave_fifoout_req` | 路由后的请求 |
| FIFO 延迟 | AO 子系统的 `obi_fifo_i` 内部 | 缓冲引入的拍数 |

### 8.2 测量端到端延迟

1. 找到 CPU 侧 `master_req_i[1].req` 的上升沿（请求发出）
2. 找到 GPIO 侧 `gpio_ao_i.i_reg_file.u_gpio_out_gpio_out_2.wr_en` 的上升沿（寄存器实际写入）
3. 两个时刻的差值 = 总线延迟（通常 2-4 个时钟周期）

---

## 课后任务

1. **地址译码练习**：CPU 执行 `gpio_write(2, true)` 时访问地址是 `0x20090180`。这个地址是经过哪两级译码到达 GPIO AO 模块的？画出从 system_xbar 到 GPIO reg_top 的地址路由路径
2. **波形观察**：在 GTKWave 中找到 `system_xbar` 内部的一次总线事务，观察 `req` → `gnt` → `rvalid` 的时序关系
3. **阅读 RTL**：打开 `xbar_varlat_n_to_one.sv`，找到 outstanding 保护 FSM 的代码，理解它为什么能防止不同 master 的事务交织
4. **计算仲裁延迟**：假设 9 个 master 同时发出请求到同一个 slave，rr_arb_tree 需要多少级才能仲裁完？

---

## 思考题

1. 如果 X-HEEP 改成双核（两个 hart），现有的 onetoM 拓扑会不会成为瓶颈？为什么？
2. `obi_fifo` 的深度为什么只有 1？如果改成深度 4 会有什么问题？
3. Error slave 的地址 `0xBADACCE5` 有什么含义？（提示：用英语念一下）
4. 一个 master 写 GPIO（地址 `0x20090180`），另一个 master 同时写 UART（地址 `0x30080000`）。在 onetoM 拓扑下，这两个事务是并行的还是串行的？

---

> **下一课预告**：第 10 课将深入每个 IP 模块的硬件实现——DMA、电源管理、中断控制器、PDM2PCM 音频通路。
