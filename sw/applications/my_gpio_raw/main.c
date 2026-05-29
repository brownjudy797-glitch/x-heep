#include <stdio.h>
#include <stdlib.h>

#include "core_v_mini_mcu.h"
#include "x-heep.h"

// GPIO 基地址（pin 2 在 AO 域 0-7，用 AO 域基地址）
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