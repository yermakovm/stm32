/**
 * @file    board.c
 * @brief   Clock tree + system-level helpers.
 *
 * Only two things are wired up on this board, so there is deliberately
 * no GPIO code here besides the alternate-function pins, which each
 * peripheral module configures itself (ilog.c for USART1,
 * i2c_bus.c for I2C1).
 */

#include "board.h"

/* Set by board_fatal() so it can be read from a debugger / core dump. */
volatile const char *board_fatal_reason;

/* HSI 16 MHz -> /8 -> *168 -> /4 => 84 MHz SYSCLK.
 * AHB = 84 MHz, APB1 = 42 MHz (I2C1, USART2), APB2 = 84 MHz
 * (USART1). FLASH latency 2 wait states. */
static void clock_config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    osc.PLL.PLLM = 8;
    osc.PLL.PLLN = 168;
    osc.PLL.PLLP = RCC_PLLP_DIV4;
    osc.PLL.PLLQ = 7;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        board_fatal("HAL_RCC_OscConfig");
    }

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) {
        board_fatal("HAL_RCC_ClockConfig");
    }

    /* SysTick feeds HAL_GetTick() -> board_ms(). */
    HAL_InitTick(TICK_INT_PRIORITY);
}

void board_init(void)
{
    if (HAL_Init() != HAL_OK) {
        board_fatal("HAL_Init");
    }
    clock_config();
}

uint32_t board_ms(void)
{
    return HAL_GetTick();
}

/* Last resort: no display/UART guarantee here, so park with interrupts
 * off and keep the reason in a global for the debugger. */
void board_fatal(const char *where)
{
    board_fatal_reason = where;
    __disable_irq();
    for (;;) {
        __NOP();
    }
}
