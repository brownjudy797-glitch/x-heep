# 第 5 课：GPIO 驱动深度剖析 — 从 C API 到硬件寄存器

## 学习目标

完成本课后，你应该能够：

- 追踪 `gpio_write()` 的完整调用链：C → MMIO → 总线 → RTL
- 理解寄存器描述文件（Hjson）如何生成 C 头文件和 RTL 代码
- 理解 MMIO 的原理：基地址 + 偏移量 → 绝对地址 → load/store 指令
- 能用纯 MMIO 操作（不调 HAL）控制 GPIO
- 在 GTKWave 中观察一次 GPIO 写操作对应的总线事务

---

## 1. 调用链路全景

以 `example_gpio_toggle/main.c` 中的 `gpio_write(2, true)` 为例，追踪完整的软硬件链路：

```
main.c:  gpio_write(2, true)
   │
   ▼
gpio.c:  gpio_perif->GPIO_OUT0 = bitfield_write(..., 1 << 2)
   │      gpio_perif = (volatile gpio *) 0x30020000
   │      GPIO_OUT0 是结构体成员，偏移 0x180
   │      最终生成: *(volatile uint32_t *)(0x30020180) = ...
   │
   ▼
RISC-V:  sw  rs2, offset(rs1)           ← 一条 store 指令
   │      rs1 = 0x30020000 + 0x180
   │
   ▼
OBI 总线: CPU 数据总线 → system_xbar → GPIO 外设端口
   │      addr = 0x30020180
   │      wdata = 0x00000004  (bit 2 = 1)
   │      we = 1
   │
   ▼
gpio_reg_top.sv: 接收 OBI 写事务，地址译码
   │      地址 0x30020180 → 偏移 0x180 → 寄存器 GPIO_OUT0
   │
   ▼
gpio.sv:  GPIO_OUT0 的 bit 2 → padout[2] → 输出引脚 → IO pad
```

这一整条链路，从 C 语言的赋值语句到物理引脚电平变化，中间经过了编译、CPU 执行、总线传输、以及硬件寄存器四个层次。

---

## 2. 第一层：HAL 驱动 API

### 2.1 gpio_write() 源码精读

`sw/device/lib/drivers/gpio/gpio.c:302-311`：

```c
gpio_result_t gpio_write(gpio_pin_number_t pin, bool val)
{
    if (pin > (MAX_PIN-1) || pin < 0)
        return GpioPinNotAcceptable;
    select_gpio_domain(pin);            // ① 选择 GPIO 域
    gpio_perif->GPIO_OUT0 = bitfield_write(gpio_perif->GPIO_OUT0,
        BIT_MASK_1, pin, val);          // ② 写寄存器
    return GpioOk;
}
```

**① 域选择** (`select_gpio_domain`, 第 137 行)：

X-HEEP 有两组 GPIO：

- **外设域 GPIO**：基地址 `0x30020000`，引脚 8-31
- **Always-On 域 GPIO**：基地址 `0x20090000`，引脚 0-7（掉电也不丢失状态）

`GPIO_AO_DOMAIN_LIMIT = 8`，所以引脚 0-7 走 AO 域，8-31 走外设域。`select_gpio_domain()` 根据引脚号切换 `gpio_perif` 指针：

```c
gpio_perif = pin < GPIO_AO_DOMAIN_LIMIT ? gpio_ao_peri : gpio_peri;
```

其中：

```c
#define gpio_peri     ((volatile gpio *) 0x30020000)  // GPIO_START_ADDRESS
#define gpio_ao_peri  ((volatile gpio *) 0x20090000)  // GPIO_AO_START_ADDRESS
```

**② 寄存器写入** (`bitfield_write`)：

`bitfield_write(old_value, mask, offset, new_value)` 的作用是：在 `old_value` 中，把 `mask` 指定的位清零，然后写入 `new_value` 到对应位置。相当于：

```c
gpio_perif->GPIO_OUT0 = (gpio_perif->GPIO_OUT0 & ~(1 << pin)) | (val << pin);
```

### 2.2 gpio_config() — 配置流程

在 `gpio_write()` 之前需要先配置引脚。`gpio_config()` 做了四件事：

```c
gpio_config(cfg):
  1. gpio_reset(pin)           // 重置引脚所有配置
  2. gpio_set_mode(pin, mode)  // 写 GPIO_MODE 寄存器（输入/输出/开漏）
  3. gpio_en_input_sampling()  // 写 GPIO_EN 寄存器（使能输入采样）
  4. gpio_intr_en()            // 写中断使能寄存器（可选）
```

引脚 2 配置为推挽输出的 `example_gpio_toggle` 等价于直接寄存器操作：

```c
// gpio_config 实际上做的寄存器写入：
GPIO_MODE0 = bitfield_write(GPIO_MODE0, 0x3, 4, 0x1);  // pin2 → 输出模式
GPIO_EN0   = bitfield_write(GPIO_EN0,   0x1, 2, 0x0);  // 禁用输入采样
```

---

## 3. 第二层：寄存器定义 — 从 Hjson 到 C 宏

### 3.1 Hjson 寄存器描述

`hw/vendor/pulp_platform/gpio/gpio_regs.hjson` 定义了 GPIO 的所有寄存器。以 `GPIO_OUT` 为例：

```json
{ multireg:
  { name: "GPIO_OUT",
    cname: "GPIO_OUT",
    count: "GPIOCount",       // 由参数 GPIOCount 决定数量（默认 32）
    compact: true,            // 紧凑模式：多个 pin 的字段合并到一个寄存器
    desc: "Set the output value of the corresponding GPIOs."
    swaccess: "rw",           // 软件可读写
    hwaccess: "hrw",          // 硬件可读写
    fields: [{ bits: "0" }]   // 每个 pin 占 1 bit
  }
}
```

GPIO 的完整寄存器 map（32 个 GPIO 引脚）：

| 寄存器                  | 偏移            | 访问 | 功能                                        |
| ----------------------- | --------------- | ---- | ------------------------------------------- |
| `INFO`                | 0x000           | RO   | 外设信息（GPIO 数量、版本号）               |
| `CFG`                 | 0x004           | RW   | 全局配置（中断模式）                        |
| `GPIO_MODE0`          | 0x008           | RW   | 引脚 0-15 的 IO 模式（每 pin 2 bit）        |
| `GPIO_MODE1`          | 0x00C           | RW   | 引脚 16-31 的 IO 模式                       |
| `GPIO_EN0`            | 0x080           | RW   | 输入采样使能（每 pin 1 bit）                |
| `GPIO_IN0`            | 0x100           | RO   | 读取输入值                                  |
| `GPIO_OUT0`           | **0x180** | RW   | **输出值 ← gpio_write 操作的寄存器** |
| `GPIO_SET0`           | 0x200           | WO   | 置位（写 1 的位将 GPIO_OUT 对应位置 1）     |
| `GPIO_CLEAR0`         | 0x280           | WO   | 清除（写 1 的位将 GPIO_OUT 对应位清 0）     |
| `GPIO_TOGGLE0`        | 0x300           | WO   | 翻转（写 1 的位将 GPIO_OUT 对应位翻转）     |
| `INTRPT_RISE_EN0`     | 0x380           | RW   | 上升沿中断使能                              |
| `INTRPT_FALL_EN0`     | 0x400           | RW   | 下降沿中断使能                              |
| `INTRPT_LVL_HIGH_EN0` | 0x480           | RW   | 高电平中断使能                              |
| `INTRPT_LVL_LOW_EN0`  | 0x500           | RW   | 低电平中断使能                              |
| `INTRPT_STATUS0`      | 0x580           | RW1C | 中断状态（写 1 清除）                       |

### 3.2 regtool.py 自动生成

`regtool.py` 读取 `.hjson` 文件，自动生成两类代码：

```
gpio_regs.hjson ──→ regtool.py ──→ gpio_regs.h      (C 头文件：偏移量宏定义)
                                └─→ gpio_reg_top.sv  (RTL：寄存器读写模块)
```

**生成的 C 头文件** (`gpio_regs.h`) 中，每个寄存器生成一组宏：

```c
// 由 Hjson 中的 {skipto: "0x180"} + {name: "GPIO_OUT"} 自动生成：
#define GPIO_GPIO_OUT_REG_OFFSET 0x180
#define GPIO_GPIO_OUT_GPIO_OUT_0_BIT 0
#define GPIO_GPIO_OUT_GPIO_OUT_1_BIT 1
// ... 一直到 BIT 31
```

这些宏在 `gpio.c` 中被间接使用：`gpio` 结构体的成员 `GPIO_OUT0` 放在了偏移 `0x180` 的位置，而那个偏移就是用这些宏定义的。

---

## 4. 第三层：MMIO — 为什么 C 赋值能操作硬件

### 4.1 结构体映射法

X-HEEP 的 GPIO 驱动没有用 `mmio_region_write32()`，而是用了**结构体指针映射法**。

`sw/device/lib/drivers/gpio/gpio_structs.h` 定义了 `gpio` 结构体，每个成员对应一个硬件寄存器：

```c
typedef volatile struct {
    uint32_t INFO;                   // offset 0x000
    uint32_t CFG;                    // offset 0x004
    uint32_t GPIO_MODE0;             // offset 0x008
    uint32_t GPIO_MODE1;             // offset 0x00C
    // ... padding ...
    uint32_t GPIO_EN0;               // offset 0x080
    // ... padding ...
    uint32_t GPIO_IN0;               // offset 0x100
    // ... padding ...
    uint32_t GPIO_OUT0;              // offset 0x180
    // ...
} gpio;
```

然后定义一个指向外设基地址的 volatile 指针：

```c
#define gpio_peri ((volatile gpio *) 0x30020000)
```

这样，`gpio_perif->GPIO_OUT0 = value` 在编译后变成：

```asm
li   a0, 0x30020000       # 加载基地址
addi a0, a0, 0x180        # 加上 GPIO_OUT0 的偏移
li   a1, 0x00000004       # pin 2 = 1
sw   a1, 0(a0)            # 存储到 MMIO 地址
```

这条 `sw`（store word）指令通过 CPU 的数据总线发出一个 OBI 写事务到地址 `0x30020180`，GPIO 硬件模块收到后更新 `GPIO_OUT0` 寄存器。

### 4.2 volatile 的作用

注意指针声明中的 `volatile`：

```c
#define gpio_peri ((volatile gpio *) 0x30020000)
```

没有 `volatile`，编译器可能会把连续两次写同一个地址优化成只写最后一次。`volatile` 告诉编译器：**每次都真的读写内存，不要优化**。

---

## 5. 第四层：OBI 总线传输

`sw` 指令执行时，CPU 的数据总线接口发起一次 OBI（Open Bus Interface）写事务。可以在 GTKWave 中观察到的关键信号：

| 信号           | 含义         | gpio_write 时的值      |
| -------------- | ------------ | ---------------------- |
| `obi_req`    | 总线请求有效 | 1                      |
| `obi_addr`   | 目标地址     | 0x30020180             |
| `obi_wdata`  | 写入数据     | 0x00000004 (bit 2 = 1) |
| `obi_we`     | 写使能       | 1                      |
| `obi_be`     | 字节使能     | 0xF (4 字节全有效)     |
| `obi_gnt`    | 总线授权     | 1（从设备准备好了）    |
| `obi_rvalid` | 响应有效     | 1（写操作完成）        |

OBI 写事务时序：

```
clk     ┌─┐  ┌─┐  ┌─┐  ┌─┐
       ─┘ └──┘ └──┘ └──┘ └──

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

一次 GPIO 写操作在总线上通常只占 1-2 个时钟周期。

### 5.1 在 GTKWave 中观察 OBI 事务

在 GTKWave SST 中展开 `x_heep_system_i → core_v_mini_mcu_i`，在 CPU 数据总线相关信号中搜索 `obi` 即可找到总线信号。把 `obi_req`、`obi_addr`、`obi_wdata`、`obi_we` 一起拖入波形，在 GPIO toggle 期间放大时间轴，就能看到每次 `gpio_write()` 对应的总线事务。

---

## 6. 第五层：GPIO 硬件 RTL

### 6.1 gpio.sv 顶层

`hw/vendor/pulp_platform/gpio/src/gpio.sv` 是 GPIO 的顶层模块，包含：

- **寄存器文件**（由 `gpio_reg_top.sv` 实现，regtool 自动生成）
- **输入采样**：同步 GPIO 输入引脚，滤除亚稳态
- **边沿检测**：检测上升沿/下降沿，产生中断请求
- **输出控制**：根据 MODE 寄存器决定引脚行为

### 6.2 gpio_reg_top.sv — 寄存器读写

`regtool.py` 生成的寄存器模块包含：

- OBI Slave 接口的地址译码逻辑
- 每个寄存器的读写控制（根据 swaccess/hwaccess 属性）
- 写使能信号：`GPIO_OUT0_we` = 1 当且仅当 `obi_addr == BASE + 0x180 && obi_we && obi_be == 4'b1111`

当收到对 `GPIO_OUT0` 的写事务时，`gpio_reg_top` 把 `obi_wdata` 的值存入 `GPIO_OUT0` 寄存器，然后连接到 `gpio.sv` 的输出控制逻辑。

### 6.3 输出通路

```
GPIO_OUT0 寄存器 (32 bit)
     │
     ▼
MODE 寄存器选择每 pin 的行为：
     ├── MODE=0 (输入): 输出驱动器高阻
     ├── MODE=1 (推挽输出): padout = GPIO_OUT[pin]
     ├── MODE=2 (开漏0):   输出 0 时驱动低，输出 1 时高阻
     └── MODE=3 (开漏1):   输出 1 时驱动高，输出 0 时高阻
     │
     ▼
IO Pad → 芯片引脚 (在 FPGA/ASIC 上)
```

---

## 7. 实践：不调 HAL，纯 MMIO 控制 GPIO

### 7.1 创建 `my_gpio_raw` application

```bash
mkdir -p /home/rime/x-heep/sw/applications/my_gpio_raw
```

`my_gpio_raw/main.c`：

```c
#include <stdio.h>
#include <stdlib.h>

#include "core_v_mini_mcu.h"
#include "x-heep.h"

// GPIO 基地址（pin 2 在 AO 域，用 AO 域基地址）
#define GPIO_BASE      0x20090000  // GPIO_AO_START_ADDRESS

// 寄存器偏移（来自 gpio_regs.h）
#define GPIO_OUT_OFFSET  0x180
#define GPIO_MODE0_OFFSET 0x008

// 寄存器地址
#define GPIO_OUT_REG     (*(volatile uint32_t *)(GPIO_BASE + GPIO_OUT_OFFSET))
#define GPIO_MODE0_REG   (*(volatile uint32_t *)(GPIO_BASE + GPIO_MODE0_OFFSET))

// bitfield 辅助宏
#define BIT_MASK_1  0x1
#define BIT_MASK_3  0x3

static inline uint32_t bitfield_write(uint32_t old, uint32_t mask, int idx, uint32_t val) {
    return (old & ~(mask << idx)) | (val << idx);
}

int main(int argc, char *argv[])
{
    // 配置 GPIO 2 为推挽输出（等价于 gpio_config 做的事）
    // MODE0 寄存器中每个 pin 占 2 bit，pin 2 在 bit[5:4]
    GPIO_MODE0_REG = bitfield_write(GPIO_MODE0_REG, BIT_MASK_3, 2 * 2, 1);

    for (int i = 0; i < 100; i++) {
        // 等价于 gpio_write(2, true)
        GPIO_OUT_REG = bitfield_write(GPIO_OUT_REG, BIT_MASK_1, 2, 1);
        for (int j = 0; j < 10; j++) asm volatile("nop");

        // 等价于 gpio_write(2, false)
        GPIO_OUT_REG = bitfield_write(GPIO_OUT_REG, BIT_MASK_1, 2, 0);
        for (int j = 0; j < 10; j++) asm volatile("nop");
    }

    printf("Raw MMIO GPIO toggle done.\n");
    return EXIT_SUCCESS;
}
```

### 7.2 编译和仿真

```bash
cd /home/rime/x-heep
make app PROJECT=my_gpio_raw
cd build/openhwgroup.org_systems_core-v-mini-mcu_1.0.5/sim-verilator
./Vtestharness +firmware=/home/rime/x-heep/sw/build/main.hex
```

---

## 8. 两种 MMIO 访问风格对比

X-HEEP 代码库中同时存在两种 MMIO 访问模式：

|                    | 结构体指针映射（GPIO 用）                        | mmio_region_xxx() 函数（I2C/SPI 用）       |
| ------------------ | ------------------------------------------------ | ------------------------------------------ |
| **写法**     | `gpio_perif->GPIO_OUT0 = val`                  | `mmio_region_write32(base, offset, val)` |
| **优点**     | 直观，像操作普通变量                             | 显式，所有 MMIO 操作可被 grep 追踪         |
| **缺点**     | 结构体成员偏移必须和硬件完全一致                 | 稍啰嗦                                     |
| **最终效果** | 完全一样：都变成 `volatile uint32_t*` 的解引用 |                                            |

两种方式最终编译成完全相同的 RISC-V load/store 指令，选择哪种纯粹是代码风格偏好。

---

## 课后任务

1. **追踪 gpio_write 调用链**：在 `gpio.c` 中从 `gpio_write()` 开始，逐行追踪到 `gpio_perif->GPIO_OUT0` 的赋值，画出调用关系图
2. **纯 MMIO 控制 GPIO**：按第 7 节创建 `my_gpio_raw`，编译并在仿真中跑通
3. **观察 OBI 总线事务**：在 GTKWave 中找到 GPIO 2 输出翻转的波形，同步观察 OBI 总线上的写事务；记录一次 gpio_write 对应的总线事务占用了几个时钟周期
4. **对比 Hjson 和生成的 C 头文件**：打开 `gpio_regs.hjson` 和 `gpio_regs.h`，对照理解哪个 Hjson 字段对应哪个 C 宏

---

## 思考题

1. 如果用 `gpio_write(2, true)` 连续写同一个 pin 两次，编译器会优化成只写一次吗？为什么？（提示：看 `gpio_perif` 的声明）
2. GPIO 的 `GPIO_SET` 和 `GPIO_CLEAR` 寄存器（偏移 0x200、0x280）与直接写 `GPIO_OUT` 有什么区别？设计这两个寄存器的目的是什么？
3. 为什么 X-HEEP 要把 GPIO 分成 AO 域（0-7）和外设域（8-31）？

---

> **下一课预告**：第 6 课将学习 UART、I2C、SPI、Timer 等外设驱动的 HAL 编程模式。
