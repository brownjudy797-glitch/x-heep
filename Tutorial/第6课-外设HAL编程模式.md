# 第 6 课：外设 HAL 编程模式 — UART、I2C、SPI、Timer

## 学习目标

完成本课后，你应该能够：
- 使用 HAL 驱动 API 操作 UART、I2C、SPI、Timer 四个核心外设
- 识别 X-HEEP 中三种不同的 HAL 设计模式，理解各自的适用场景
- 理解 SDK 层在 HAL 之上额外封装了什么
- 能阅读外设驱动的源码，从 HAL 追踪到 MMIO

---

## 1. 概览：外设与地址映射

X-HEEP 的外设分为两个域，各自有独立的基地址段：

| 外设 | 基地址 | 所在域 | 中断号 |
|------|--------|--------|--------|
| 外设域 GPIO | `0x30020000` | 外设域 | — |
| 外设域 UART | `0x30080000` | 外设域 | 1-8 |
| 外设域 I2C | `0x30030000` | 外设域 | 13 |
| 外设域 SPI Host | `0x30010000` | 外设域 | 9-12 / 14-17 |
| 外设域 Timer | `0x30040000` | 外设域 | 18 |
| AO 域 GPIO | `0x20090000` | Always-On 域 | — |
| AO 域 Timer | `0x20050000` | Always-On 域 | — |
| SPI Flash | `0x20020000` | Always-On 域 | 5-8 |

每个外设都有对应的寄存器描述文件（Hjson）和自动生成的寄存器头文件：

```
hw/vendor/.../uart/data/uart.hjson  →  sw/.../uart/uart_regs.h
hw/vendor/.../i2c/data/i2c.hjson    →  sw/.../i2c/i2c_regs.h
hw/vendor/.../spi/data/spi_host.hjson → sw/.../spi_host/spi_host_regs.h
hw/vendor/.../rv_timer/data/rv_timer.hjson → sw/.../rv_timer/rv_timer_regs.h
```

---

## 2. UART — 最简轮询模式

### 2.1 寄存器

UART 寄存器简洁（共 11 个），核心是四个：

| 寄存器 | 偏移 | 功能 |
|--------|------|------|
| `CTRL` | `0x0C` | 控制：TX 使能、RX 使能、校验位、NCO 值 |
| `STATUS` | `0x10` | 状态：TXFULL、RXFULL、TXEMPTY、TXIDLE、RXIDLE、RXEMPTY |
| `RDATA` | `0x14` | 读数据（收到的一个字节） |
| `WDATA` | `0x18` | 写数据（发送的一个字节） |

### 2.2 HAL API

```c
// 初始化
system_error_t uart_init(const uart_t *uart);

// 发送
void uart_putchar(const uart_t *uart, uint8_t byte);
size_t uart_write(const uart_t *uart, const uint8_t *data, size_t len);

// 接收
size_t uart_getchar(const uart_t *uart, uint8_t *data);
size_t uart_read(const uart_t *uart, const uint8_t *data, size_t len);
```

`uart_t` 结构体：

```c
typedef struct uart {
    mmio_region_t base_addr;   // MMIO 基地址
    uint32_t      baudrate;    // 波特率
    uint64_t      nco;         // 波特率生成用的 NCO 值
    uint32_t      clk_freq_hz; // 外设时钟频率
} uart_t;
```

### 2.3 使用示例

```c
#include "core_v_mini_mcu.h"
#include "uart.h"
#include "x-heep.h"

// printf 最终就是通过这个函数输出到 UART
static uart_t my_uart = {
    .base_addr   = (mmio_region_t)UART_START_ADDRESS,  // 0x30080000
    .baudrate    = UART_BAUDRATE,
    .clk_freq_hz = REFERENCE_CLOCK_Hz,
};

int main(void)
{
    uart_init(&my_uart);

    // 发送单个字符
    uart_putchar(&my_uart, 'A');

    // 发送字符串
    uart_write(&my_uart, (uint8_t *)"Hello\n", 6);

    // 接收一个字节
    uint8_t c;
    uart_getchar(&my_uart, &c);  // 阻塞等待
}
```

### 2.4 发送的数据流

```
uart_putchar(&my_uart, 'A')
  → 检查 STATUS 寄存器的 TXFULL 位（没满才能写）
  → 把 'A' 写入 WDATA 寄存器
  → 等待 STATUS 的 TXIDLE 位置位（发送完毕）
  → 返回

实际 MMIO 操作：
  while (mmio_read32(base + 0x10) & TXFULL);  // 等发送槽空闲
  mmio_write32(base + 0x18, 'A');              // 写入数据
  while (!(mmio_read32(base + 0x10) & TXIDLE)); // 等发送完成
```

UART 没有 SDK 层封装——`printf` 最终直接调用 `uart_sink()`，而 `uart_sink()` 调用 `uart_write()`。链路是：

```
printf("hello") → _write() → uart_sink() → uart_write() → MMIO
```

---

## 3. I2C — DIF 风格（Init / Configure 分离）

I2C 驱动来自 OpenTitan DIF（Device Interface Functions）框架，体现了一种更规范的 HAL 设计风格。

### 3.1 DIF 风格的特点

```
i2c_init()        → 填充内部结构体参数
i2c_compute_timing() → 根据速率计算时序参数（独立于硬件）
i2c_configure()   → 把配置写入硬件寄存器
i2c_host_set_enabled() → 使能主机模式
```

核心思想是**参数计算和硬件操作分离**——`i2c_compute_timing()` 是纯软件计算，不碰寄存器，方便单元测试。

### 3.2 关键类型

```c
// 速度等级
typedef enum {
    kDifI2cSpeedStandard,  // 100 kHz
    kDifI2cSpeedFast,      // 400 kHz
    kDifI2cSpeedFastPlus,  // 1 MHz
} i2c_speed_t;

// 格式码（I2C 帧格式控制）
typedef enum {
    kDifI2cFmtStart,       // 产生 START 条件
    kDifI2cFmtTx,          // 发送一个字节 + ACK
    kDifI2cFmtTxStop,      // 发送最后一个字节 + STOP
    kDifI2cFmtRx,          // 读一个字节 + ACK
    kDifI2cFmtRxContinue,  // 读一个字节 + ACK + 继续
    kDifI2cFmtRxStop,      // 读最后一个字节 + NAK + STOP
} i2c_fmt_t;
```

### 3.3 HAL API

I2C 不提供 "write_byte / read_byte" 这种看起来直接的操作，而是暴露**格式 FIFO** 机制：

```c
i2c_write_byte(..., uint8_t byte, i2c_fmt_t code, bool suppress_nak_irq);
i2c_read_byte(const i2c_t *i2c, uint8_t *byte);
```

发一个完整的 I2C 写事务需要自己拼格式序列：

```c
// 写设备地址 + 寄存器地址 + 数据
i2c_write_byte(&i2c, dev_addr << 1,  kDifI2cFmtStart, false);  // START + 设备地址
i2c_write_byte(&i2c, reg_addr,       kDifI2cFmtTx,    false);  // 寄存器地址
i2c_write_byte(&i2c, data,           kDifI2cFmtTxStop,false);  // 数据 + STOP
```

这就是为什么 I2C 需要 SDK 层——手动拼格式码太繁琐。

### 3.4 SDK 封装

```c
// SDK 把格式码细节屏蔽掉
i2c_sdk_write(dev_addr, reg, source, num_bytes);
i2c_sdk_read(dev_addr, reg, destination, num_bytes);
```

`i2c_sdk_write()` 内部做了的事：

```
initialize_i2c():                    (只需调用一次)
  → 从 SOC_CTRL 读取系统时钟频率
  → i2c_init()
  → i2c_compute_timing()            // 根据频率算时序
  → i2c_configure()                 // 写时序到寄存器
  → i2c_set_watermarks()
  → i2c_host_set_enabled()

i2c_sdk_write(dev, reg, data, n):
  → i2c_write_byte(dev_addr, kDifI2cFmtStart)
  → i2c_write_byte(reg,      kDifI2cFmtTx)
  → for each byte: i2c_write_byte(data[i], kDifI2cFmtTx)
  → i2c_write_byte(last,     kDifI2cFmtTxStop)
```

### 3.5 中断管理

I2C 有 9 种中断源（FMT watermark、RX watermark、溢出、NAK、SCL 干扰等），HAL 提供成组的中断管理 API：

```c
i2c_irq_is_pending(...)     // 查哪个中断在等待
i2c_irq_acknowledge(...)    // 清除中断
i2c_irq_set_enabled(...)    // 使能/禁用特定中断
i2c_irq_disable_all(...)    // 批量禁用（保存当前状态）
i2c_irq_restore_all(...)    // 批量恢复
```

---

## 4. SPI Host — 直接结构体指针风格

SPI Host 驱动是 X-HEEP 中最独特的设计，它不用 `mmio_region_t`，而是直接用结构体指针映射。

### 4.1 三组宏定义

```c
#define spi_host1  ((spi_host_t*) spi_host1_peri)   // SPI1: 0x30010000
#define spi_host2  ((spi_host_t*) spi_host2_peri)   // SPI2: 0x30050000
#define spi_flash  ((spi_host_t*) spi_flash_peri)   // Flash SPI: 0x20020000
```

这和 GPIO 的 `gpio_perif->GPIO_OUT0` 是同一模式（第 5 课的结构体映射法）。

### 4.2 HAL API

SPI 的核心抽象是**命令 + 配置**：

```c
// 配置
spi_set_configopts(spi, csid, conf_reg);  // 时钟分频、相位、极性、CS 时序
spi_set_csid(spi, csid);                  // 选择片选

// 执行
spi_set_command(spi, cmd_reg);            // 长度 + 方向 + 速度
spi_write_word(spi, wdata);               // 写数据
spi_read_word(spi, &rdata);               // 读数据

// 等待
spi_wait_for_ready(spi);                  // 等外设就绪
spi_wait_for_idle(spi);                   // 等传输完成
spi_wait_for_rx_not_empty(spi);           // 等接收数据
spi_wait_for_tx_not_full(spi);            // 等发送槽空闲
```

### 4.3 配置和命令

```c
// 配置参数
typedef struct {
    uint16_t clkdiv;     // 时钟分频（SPI 时钟 = 系统时钟 / (2*(clkdiv+1))）
    bool     csnidle;    // 空闲时 CS 电平
    uint8_t  csntrail;   // CS 尾延时（半周期数）
    uint8_t  csnlead;    // CS 前延时（半周期数）
    bool     fullcyc;    // 全周期模式
    bool     cpha;       // 时钟相位
    bool     cpol;       // 时钟极性（CPOL=0 空闲低，CPOL=1 空闲高）
} spi_configopts_t;

// 命令参数
typedef struct {
    uint8_t  len;        // 传输长度（字节数，最大 24 位）
    bool     csaat;      // 传输完不拉高 CS（用于连续传输）
    uint8_t  speed;      // 标准/双/四线
    uint8_t  direction;  // 方向：dummy / tx only / rx only / bidir
} spi_command_t;
```

### 4.4 发送流程

```c
// SPI 发送三个字节：0xAA 0xBB 0xCC
spi_set_configopts(spi_host1, 0, spi_create_configopts(cfg));  // 配置 SPI 模式
spi_set_csid(spi_host1, 0);                                     // 选 CS0

spi_command_t cmd = { .len = 3, .direction = kTxOnly, .speed = kStandard };
spi_set_command(spi_host1, spi_create_command(cmd));            // 设定传输参数

spi_wait_for_tx_not_full(spi_host1);                            // 等发送槽
spi_write_word(spi_host1, 0xAABBCC);                            // 写入数据（合并为一个 word）
spi_wait_for_idle(spi_host1);                                   // 等传输完成
```

### 4.5 SDK 封装

SPI SDK 在 HAL 之上加了状态机和多种传输模式：

```c
// 阻塞式
spi_transmit(spi, src_buffer, len);   // 纯发送
spi_receive(spi, dest_buffer, len);   // 纯接收
spi_transceive(spi, src, dest, len);  // 同时收发

// 非阻塞式（带回调）
spi_transmit_nb(spi, src_buffer, len, callbacks);
spi_transceive_nb(spi, src, dest, len, callbacks);

// 复杂多段传输（同一个 CS 下拼多种操作）
spi_execute(spi, segments, num_segments, ...);
```

---

## 5. RV Timer — 计数器 + 比较器模式

### 5.1 硬件结构

RV Timer 是一个多 hart、多比较器的 64 位计数器：

```
  ┌─────────────────────────────────────┐
  │        RV Timer                     │
  │                                     │
  │  计数器 (64bit) ── 持续递增 ──→    │
  │       │                             │
  │       ├── 比较器 0 ──→ 匹配 → IRQ  │
  │       ├── 比较器 1 ──→ 匹配 → IRQ  │
  │       └── ...                       │
  │                                     │
  │  预分频器 (prescale) 控制计数频率   │
  └─────────────────────────────────────┘
```

计数频率 = 系统时钟 / (prescale + 1)。比如 prescale=99，则每 100 个时钟周期计数器加 1。

### 5.2 HAL API

```c
// 初始化
rv_timer_init(base_addr, config, &timer);
rv_timer_approximate_tick_params(clock_freq, counter_freq, &out);  // 算 prescale

// 计数器控制
rv_timer_counter_set_enabled(&timer, hart_id, kRvTimerEnabled);
rv_timer_counter_read(&timer, hart_id, &count);

// 比较器
rv_timer_arm(&timer, hart_id, comp_id, threshold);   // 设闹钟：计数器到 threshold 时触发
rv_timer_irq_enable(&timer, hart_id, comp_id, kRvTimerEnabled);
rv_timer_irq_get(&timer, hart_id, comp_id, &flag);   // 查中断是否触发了
rv_timer_irq_clear(&timer, hart_id, comp_id);        // 清中断
```

### 5.3 SDK 封装

Timer SDK 把 hart/comparator 的概念全部隐藏，提供最直观的 API：

```c
timer_start();                         // 开始计时
uint32_t elapsed = timer_stop();       // 停止并获取经过的周期数
timer_wait_us(1000);                  // 阻塞等待 1000 微秒
timer_arm_start(threshold);            // 设置定时器中断
```

### 5.4 使用示例

```c
#include "timer_sdk.h"
#include "rv_timer.h"

// 中断处理函数（名字必须匹配）
void __attribute__((aligned(4), interrupt)) handler_irq_timer(void) {
    timer_arm_stop();
    timer_irq_clear();
    // 这里做你想在定时器触发后执行的事
}

int main(void)
{
    // 初始化计时器（使用系统时钟频率）
    timer_cycles_init();
    enable_timer_interrupt();

    // 用法一：测量代码执行时间
    timer_start();
    my_function();
    uint32_t cycles = timer_stop();
    printf("my_function took %lu cycles\n", cycles);

    // 用法二：定时器中断
    timer_arm_start(100000);  // 100000 个时钟周期后触发中断
    while (1) { asm volatile("wfi"); }

    // 用法三：阻塞延时
    timer_wait_us(500);       // 等 500 微秒
}
```

### 5.5 call chain

```
timer_wait_us(500)
  → rv_timer_counter_set_enabled(...)     // 启动计数器
  → rv_timer_arm(hart0, comp0, threshold) // 设阈值
  → wfi                                  // 等中断唤醒
  → handler_irq_timer()                  // 中断触发
      → timer_arm_stop()
      → timer_irq_clear()
  → 返回，延时结束
```

---

## 6. 三种 HAL 设计模式对比

X-HEEP 里同时存在三种 HAL 编程风格，理解每种的特点有助于阅读和扩展驱动代码：

| | 简单风格（UART） | DIF 风格（I2C、Timer） | 结构体指针风格（SPI Host、GPIO） |
|---|---|---|---|
| **访问方式** | `mmio_region_t` | `mmio_region_t` | `volatile struct*` 直接映射 |
| **初始化** | 单步 `_init()` | 多步：`_init()` → `_compute()` → `_configure()` | 直接写寄存器 |
| **中断管理** | weak handler | `_irq_enable/disable/pending/ack` 全套 API | 独立的 event/error 中断 |
| **复杂度** | 低 | 中 | 高 |
| **来源** | X-HEEP 自研 | OpenTitan DIF 规范 | X-HEEP 自研 |
| **代表外设** | UART | I2C, RV Timer | SPI Host, SPI Flash, GPIO |

三种风格编译后都是同一条 RISC-V `sw` / `lw` 指令，设计差异只影响代码可读性和可维护性。

---

## 7. HAL / SDK 分层总结

```
applications/         调用 SDK 或直接调 HAL
───────────────────────────────────────────
sdk/                  高层封装，屏蔽外设细节
  i2c_sdk:            i2c_sdk_write() / i2c_sdk_read()
  spi_sdk:            spi_transmit() / spi_transceive()
  timer_sdk:          timer_wait_us() / timer_start()
───────────────────────────────────────────
drivers/              HAL 驱动，直接操作寄存器
  uart:               uart_write() / uart_getchar()
  i2c:                i2c_write_byte() / i2c_read_byte()
  spi_host:           spi_write_word() / spi_read_word()
  rv_timer:           rv_timer_arm() / rv_timer_counter_read()
───────────────────────────────────────────
base/                 MMIO 基础操作
  mmio_read32() / mmio_write32() (或结构体指针)
```

---

## 8. 实践任务

### 8.1 用纯 HAL 操作 UART

创建 `my_uart_hal/main.c`：

```c
#include <stdio.h>
#include <stdlib.h>
#include "core_v_mini_mcu.h"
#include "uart.h"
#include "x-heep.h"

int main(void)
{
    uart_t uart = {
        .base_addr   = mmio_region_from_addr(UART_START_ADDRESS),
        .baudrate    = UART_BAUDRATE,
        .clk_freq_hz = REFERENCE_CLOCK_Hz,
        .nco         = UART_NCO,
    };
    uart_init(&uart);

    char *msg = "UART HAL test passed!\r\n";
    uart_write(&uart, (uint8_t *)msg, 22);

    return EXIT_SUCCESS;
}
```

### 8.2 用 Timer SDK 测代码速度

修改 `example_gpio_toggle/main.c`，在 loop 前后加 timer：

```c
#include "timer_sdk.h"

timer_cycles_init();
timer_start();
for (int i = 0; i < 100; i++) {
    gpio_write(2, true);
    gpio_write(2, false);
}
uint32_t elapsed = timer_stop();
float us = get_time_from_cycles(elapsed);
printf("100 GPIO toggles: %lu cycles = %.2f us\n", elapsed, us);
```

### 8.3 阅读驱动源码

任选一个外设的 `.c` 文件，找一个你感兴趣的 API 函数，追踪到 `mmio_region_write32()`，列出中间的调用链。

---

## 9. 常见陷阱

### 9.1 `mmio_region_t` 不能直接强制转换

`mmio_region_t` 是一个结构体（包含 `volatile void *base`），不是整数。C 语言不允许将整数强制转换为结构体类型：

```c
// ❌ 错误 — 编译报错
.base_addr = (mmio_region_t)UART_START_ADDRESS,

// ✅ 正确 — 用 mmio.h 提供的构造器
.base_addr = mmio_region_from_addr(UART_START_ADDRESS),
```

`mmio_region_from_addr()` 是一个 inline 函数，用复合字面量正确初始化结构体。

### 9.2 仿真中的死循环

Verilator 仿真没有"超时自动杀进程"的机制——如果 `main()` 不返回或没有其它退出条件，仿真**永远不结束**。

```c
// ❌ 死循环 — 仿真卡住
while (1) {
    // 等 UART 输入、等中断、等标志位……
}

// ✅ 让 main 返回
return EXIT_SUCCESS;
```

`main()` 返回后，C 运行时会调用 `_exit()`，写 `soc_ctrl` 的 `EXIT_VALID` 寄存器，testbench 读到后结束仿真。不需要 `+max_sim_time=`。

### 9.3 UART 缺少 `nco` 字段 → 死等

这是最容易踩的坑：`uart_t` 结构体有四个字段，漏掉 `.nco` 编译不会报错（C 语言自动初始化为 0），但运行时**死等在 `uart_tx_idle()`**。

原因：`nco == 0` 意味着波特率分频系数为零，UART 内核不发时钟脉冲，transmitter 永远不会启动，`TXIDLE` 永远为 0。

```c
// ❌ 漏了 nco → uart_putchar 死等
uart_t uart = {
    .base_addr   = mmio_region_from_addr(UART_START_ADDRESS),
    .baudrate    = UART_BAUDRATE,
    .clk_freq_hz = REFERENCE_CLOCK_Hz,
    // .nco 未初始化 → 0 → UART 硬件不工作
};

// ✅ 补齐四个字段
uart_t uart = {
    .base_addr   = mmio_region_from_addr(UART_START_ADDRESS),
    .baudrate    = UART_BAUDRATE,
    .clk_freq_hz = REFERENCE_CLOCK_Hz,
    .nco         = UART_NCO,
};
```

`UART_BAUDRATE` 和 `UART_NCO` 都在 `x-heep.h` 中预定义好了，直接用就行。其中 NCO 的计算公式：

```
NCO = (BAUDRATE × 16 × 65536) / REFERENCE_CLOCK_Hz
```

这是 OpenTitan UART IP 的固定精度分频算法，16× 过采样，16-bit 小数精度。

### 9.4 浮点 printf 不输出

X-HEEP 用的 newlib-nano 默认不链接浮点格式化代码（省 ~10 KB ROM）。`printf("%f", ...)` 输出为空。

```c
// ❌ 输出为空
float us = get_time_from_cycles(elapsed);
printf("%.2f us\n", us);

// ✅ 用整数计算
uint32_t us = elapsed / (REFERENCE_CLOCK_Hz / 1000000);
printf("%lu us\n", us);
```

如果确实需要浮点 printf，在 CMakeLists.txt 的链接选项中加入 `-u _printf_float`。

---

## 课后任务

1. **UART 实战**：按 8.1 节创建 `my_uart_hal`，编译并在仿真中跑通
2. **Timer 测速**：按 8.2 节在 GPIO toggle 中加入计时器，对比一个循环的时钟周期数
3. **Grep 统计**：在 `sw/device/lib/drivers/` 下用 `grep -r "mmio_region_write32" --include="*.c" | wc -l` 看看 HAL 层有多少次 MMIO 写调用
4. **对比 SDK vs HAL**：阅读 `i2c_sdk.c` 中的 `i2c_sdk_write()`，和 `i2c.c` 中的 `i2c_write_byte()` 对比，找出 SDK 替你做了哪些 HAL 层的琐碎操作

---

## 思考题

1. UART 为什么不需要 SDK 层，而 I2C 和 SPI 需要？SPI 驱动已经有 `spi_write_word()`，为什么还需要 `spi_sdk_transmit()`？
2. Timer 的 `rv_timer_approximate_tick_params()` 做了什么？为什么 timer 需要预分频器（prescaler）？
3. I2C 的格式 FIFO（Format FIFO）是什么概念？为什么 I2C 需要格式码（START / TX / STOP），而 SPI 不需要？

---

> **下一课预告**：第 7 课将学习中断系统——PLIC 中断控制器、外部中断处理流程、以及如何在驱动中使用中断代替轮询。
