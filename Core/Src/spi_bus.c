/**
 * @file    spi_bus.c
 * @brief   Blocking SPI2 master with a manual chip select.
 *
 * Mode 0 (CPOL = 0, CPHA = 0), which is what the BME280 SPI interface
 * uses. Transfers are small: the biggest one in this project is the
 * 26-byte calibration burst at boot, about 160 us at 1.3 MHz.
 */

#include "spi_bus.h"

#include "app_config.h"
#include "ilog.h"

static SPI_HandleTypeDef s_hspi;
static uint32_t s_errors;

void spi_bus_cs(bool asserted)
{
    HAL_GPIO_WritePin(SPI_BUS_CS_PORT, SPI_BUS_CS_PIN,
                      asserted ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

bool spi_bus_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_SPI2_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* PB13 SCK, PB14 MISO, PB15 MOSI (AF5). */
    gpio.Pin = GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_MEDIUM;
    gpio.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* CS idles high so the sensor never sees a half transaction. */
    gpio.Pin = SPI_BUS_CS_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = 0;
    HAL_GPIO_Init(SPI_BUS_CS_PORT, &gpio);
    spi_bus_cs(false);

    s_hspi.Instance = SPI_BUS_INSTANCE;
    s_hspi.Init.Mode = SPI_MODE_MASTER;
    s_hspi.Init.Direction = SPI_DIRECTION_2LINES;
    s_hspi.Init.DataSize = SPI_DATASIZE_8BIT;
    s_hspi.Init.CLKPolarity = SPI_POLARITY_LOW;   /* CPOL 0 */
    s_hspi.Init.CLKPhase = SPI_PHASE_1EDGE;       /* CPHA 0 */
    s_hspi.Init.NSS = SPI_NSS_SOFT;
    s_hspi.Init.BaudRatePrescaler = SPI_BUS_PRESCALER;
    s_hspi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    s_hspi.Init.TIMode = SPI_TIMODE_DISABLE;
    s_hspi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    s_hspi.Init.CRCPolynomial = 7;
    if (HAL_SPI_Init(&s_hspi) != HAL_OK) {
        LOGE("spi: SPI2 init failed");
        s_errors++;
        return false;
    }

    /* SPI2 sits on APB1 (42 MHz): 42 MHz / 2^prescaler is the SCK rate. */
    LOGI("spi: SPI2 up, sck=%lu Hz (prescaler %u), cs=PB%u",
         (unsigned long)(42000000U / (1U << SPI_BUS_PRESCALER)),
         (unsigned)SPI_BUS_PRESCALER, 12U);
    return true;
}

/* A delay the driver needs between bus polls. HAL_Delay is the only
 * timer this firmware has, so the granularity is 1 ms. */
void spi_bus_wait_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

/* What the pins and the peripheral actually look like, for "is the
 * sensor connected at all?" debugging. */
void spi_bus_diag(const char *why)
{
    const uint32_t cr1 = s_hspi.Instance->CR1;
    const uint32_t sr = s_hspi.Instance->SR;

    LOGI("spi: diag (%s)", why);
    /* With CS idle high the slave must not drive MISO, so the line is
     * whatever the pad + pull configuration makes it. SCK idles low in
     * mode 0. A stuck SCK/MOSI means the pin is not the one wired. */
    LOGI("spi:   pads cs=PB12:%u sck=PB13:%u miso=PB14:%u mosi=PB15:%u",
         (unsigned)(HAL_GPIO_ReadPin(SPI_BUS_CS_PORT, SPI_BUS_CS_PIN) == GPIO_PIN_SET),
         (unsigned)(HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_13) == GPIO_PIN_SET),
         (unsigned)(HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_14) == GPIO_PIN_SET),
         (unsigned)(HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_15) == GPIO_PIN_SET));
    LOGI("spi:   cr1=0x%x sr=0x%x state=%d er=0x%x",
         (unsigned)cr1, (unsigned)sr, (int)HAL_SPI_GetState(&s_hspi),
         (unsigned)HAL_SPI_GetError(&s_hspi));
    /* CR1 bit 6 = SPE (peripheral enabled). SR: bit0 TXE, bit1 RXNE,
     * bit3 BSY, bit4 MODF, bit6 OVR. */
    if ((cr1 & (1U << 6)) == 0U) {
        LOGE("spi:   SPI2 is not enabled (SPE=0) - init failed");
    }
    if ((sr & (1U << 3)) != 0U) {
        LOGE("spi:   SPI2 still busy - a previous transfer never finished");
    }
    if ((sr & (1U << 4)) != 0U) {
        LOGE("spi:   mode fault: SCK is driven from outside (pin conflict?)");
    }
    if ((sr & (1U << 6)) != 0U) {
        LOGE("spi:   overrun on RX - the clock is faster than the reads");
    }
}

bool spi_bus_transfer(const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    bool ok;

    if (len == 0U || (tx == NULL && rx == NULL)) {
        return false;
    }

    if (rx == NULL) {
        ok = (HAL_SPI_Transmit(&s_hspi, (uint8_t *)tx, len, SPI_TIMEOUT_MS)
              == HAL_OK);
    } else if (tx == NULL) {
        /* MOSI is don't-care while the slave clocks data out. */
        ok = (HAL_SPI_Receive(&s_hspi, rx, len, SPI_TIMEOUT_MS) == HAL_OK);
    } else {
        ok = (HAL_SPI_TransmitReceive(&s_hspi, (uint8_t *)tx, rx, len,
                                      SPI_TIMEOUT_MS) == HAL_OK);
    }

    if (!ok) {
        s_errors++;
        /* ErrorCode is a HAL_SPI_ERROR_* bitmask (1 MODF, 2 CRC, 4 OVR,
         * 8 FRE, 0x20 TXE/RXNE/BSY flag timeout, 0x40 abort). A timeout
         * here means the peripheral never saw the clock/data it waited
         * for: no slave, or SCK/MOSI/MISO on the wrong pins. */
        LOGE("spi: %u-byte %s failed (state %d, err 0x%x, sr=0x%x)",
             (unsigned)len,
             (rx != NULL) ? "read" : "write",
             (int)HAL_SPI_GetState(&s_hspi),
             (unsigned)HAL_SPI_GetError(&s_hspi),
             (unsigned)s_hspi.Instance->SR);
    }
    return ok;
}

bool spi_bus_is_ready(void)
{
    return HAL_SPI_GetState(&s_hspi) == HAL_SPI_STATE_READY;
}

uint32_t spi_bus_error_count(void)
{
    return s_errors;
}
