/* STM32F401CE (Black Pill) <-> ESP32-S3 UART link
 *
 * USART1 (PA9/PA10)  @115200 -> debug log to PC via ST-Link USB
 * USART2 (PA2/PA3)   @115200 -> ESP32: ESP GPIO17->PA3, GPIO18<-PA2, GND<->GND
 * Frame (both dirs):  0xA5 | cmd | header^cmd     (CMD_TOGGLE_LEDS = 0x01)
 * PA15 = button (active-low), PC13 = onboard LED (active-low)
 */

#include "main.h"
#include <string.h>

#define FRAME_HEADER     0xA5U
#define CMD_TOGGLE_LEDS  0x01U
#define FRAME_LEN        3U

UART_HandleTypeDef huart1;  /* debug log */
UART_HandleTypeDef huart2;  /* ESP32 link */

uint8_t rx_frame[FRAME_LEN];
uint32_t rx_pos = 0;
int btn_last = 0, btn_raw = 0;
uint32_t btn_change_ms = 0;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);

static void log_str(const char *s)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)s, strlen(s), 100);
}

static void log_hex8(uint8_t val)
{
    char buf[4];
    const char hex[] = "0123456789ABCDEF";
    buf[0] = hex[(val >> 4) & 0x0F];
    buf[1] = hex[val & 0x0F];
    buf[2] = '\r';
    buf[3] = '\n';
    HAL_UART_Transmit(&huart1, (uint8_t *)buf, 4, 100);
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART1_UART_Init();
    MX_USART2_UART_Init();

    /* Boot sanity blink */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
    HAL_Delay(200);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    HAL_Delay(200);

    log_str("\r\n========== BOOT ==========\r\n");
    log_str("USART1 debug + USART2 ESP link ready\r\n");
    log_str("============================\r\n");

    /* Let boot log flush, then drain any garbage in the USART2 RX register */
    HAL_Delay(200);
    uint8_t drain;
    while (HAL_UART_Receive(&huart2, &drain, 1, 0) == HAL_OK) {}

    while (1)
    {
        /* RX from ESP32. Never log per byte: blocking at ~90 us/char is slower
         * than one incoming byte (87 us) and would overrun USART2 RX. Log only
         * once a whole frame is parsed. */
        uint8_t rx_byte = 0;
        if (HAL_UART_Receive(&huart2, &rx_byte, 1, 10) == HAL_OK)
        {
            if (rx_pos == 0)
            {
                if (rx_byte == FRAME_HEADER) rx_frame[rx_pos++] = rx_byte;
            }
            else
            {
                rx_frame[rx_pos++] = rx_byte;
                if (rx_pos == FRAME_LEN)
                {
                    uint8_t crc_calc = rx_frame[0] ^ rx_frame[1];
                    rx_pos = 0;
                    if (rx_frame[2] != crc_calc)
                    {
                        log_str("BAD ");
                        log_hex8(rx_frame[0]);
                        log_hex8(rx_frame[1]);
                        log_hex8(rx_frame[2]);
                    }
                    else if (rx_frame[1] == CMD_TOGGLE_LEDS)
                    {
                        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
                        log_str("TOGGLE\r\n");
                    }
                }
            }
        }

        /* Button PA15: send one frame per press edge, after 30 ms of stability. */
        int raw = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15) == GPIO_PIN_RESET) ? 1 : 0;
        if (raw != btn_raw)
        {
            btn_raw = raw;
            btn_change_ms = HAL_GetTick();
        }
        if ((HAL_GetTick() - btn_change_ms >= 30) && (btn_raw != btn_last))
        {
            btn_last = btn_raw;
            if (btn_raw)
            {
                const uint8_t frame[3] = {FRAME_HEADER, CMD_TOGGLE_LEDS, FRAME_HEADER ^ CMD_TOGGLE_LEDS};
                HAL_UART_Transmit(&huart2, (uint8_t *)frame, 3, 100);
                log_str("BTN\r\n");
            }
        }
    }
}

/* HSI 16 MHz -> /8 *168 /4 = 84 MHz SYSCLK; APB1 = 42 MHz, APB2 = 84 MHz */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = 8;
    RCC_OscInitStruct.PLL.PLLN = 168;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
    RCC_OscInitStruct.PLL.PLLQ = 7;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                                |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
    {
        Error_Handler();
    }
}

/* USART1: debug log to PC (PA9 TX / PA10 RX, pins set in HAL_UART_MspInit) */
static void MX_USART1_UART_Init(void)
{
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK)
    {
        Error_Handler();
    }
}

/* USART2: ESP32 link */
static void MX_USART2_UART_Init(void)
{
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart2) != HAL_OK)
    {
        Error_Handler();
    }
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PC13: onboard LED, active-low -> off */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* PA15: button, active-low with internal pull-up */
    GPIO_InitStruct.Pin = GPIO_PIN_15;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
    Error_Handler();
}
#endif
