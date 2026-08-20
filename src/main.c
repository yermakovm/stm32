#include <stdint.h>

#define RCC_BASE        0x40023800UL
#define GPIOC_BASE      0x40020800UL

#define RCC_AHB1ENR     (*(volatile uint32_t*)(RCC_BASE + 0x30))
#define GPIOC_MODER     (*(volatile uint32_t*)(GPIOC_BASE + 0x00))
#define GPIOC_ODR       (*(volatile uint32_t*)(GPIOC_BASE + 0x14))

static void delay(uint32_t count)
{
    while (count--)
    {
        __asm__("nop");
    }
}

int main(void)
{
    // Enable GPIOC clock
    RCC_AHB1ENR |= (1 << 2);

    // PC13 -> Output mode
    GPIOC_MODER &= ~(3U << (13 * 2));
    GPIOC_MODER |=  (1U << (13 * 2));

    while (1)
    {
        GPIOC_ODR ^= (1 << 13);
        delay(1000000);
    }
}
