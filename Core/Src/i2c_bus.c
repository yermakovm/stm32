/**
 * @file    i2c_bus.c
 * @see     i2c_bus.h
 *
 * One I2C1 instance owns both the display and the RTC/EEPROM, so every
 * driver here runs from the same superloop (no interrupts, no DMA) and
 * a failed transfer is reported instead of fatal.
 */

#include "i2c_bus.h"

#include "board.h"
#include "ilog.h"

static I2C_HandleTypeDef s_i2c;
static uint32_t s_errors;
static uint32_t s_streak;

/* ------------------------------------------------------------------ */
/* init / recovery                                                     */
/* ------------------------------------------------------------------ */

static void pins_config(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* Open-drain + AF4: the pull-ups live on the modules. With no
     * pull-up at all the lines would float, so keep the internal one
     * enabled as a safety net. */
    gpio.Pin = BOARD_I2C_SCL_PIN | BOARD_I2C_SDA_PIN;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = BOARD_I2C_AF;
    HAL_GPIO_Init(BOARD_I2C_GPIO, &gpio);
}

bool i2c_bus_init(void)
{
    (void)HAL_I2C_DeInit(&s_i2c);
    __HAL_RCC_I2C1_CLK_ENABLE();

    s_i2c.Instance = I2C1;
    s_i2c.Init.ClockSpeed = I2C_BUS_CLK_SPEED;
    s_i2c.Init.DutyCycle = I2C_DUTYCYCLE_2;
    s_i2c.Init.OwnAddress1 = 0;
    s_i2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    s_i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    s_i2c.Init.OwnAddress2 = 0;
    s_i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    s_i2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&s_i2c) != HAL_OK) {
        return false;
    }
    pins_config();

    s_streak = 0U;
    return true;
}

/* A slave that got reset mid-transfer can keep SDA held low, which
 * makes the peripheral hang on "BUSY". Clock SCL by hand until SDA
 * is released again, then bring the peripheral back up. */
static bool bus_recover(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint32_t pulses;

    LOGT("i2c bus recovery");
    (void)HAL_I2C_DeInit(&s_i2c);

    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin = BOARD_I2C_SCL_PIN | BOARD_I2C_SDA_PIN;
    HAL_GPIO_Init(BOARD_I2C_GPIO, &gpio);
    HAL_GPIO_WritePin(BOARD_I2C_GPIO,
                      BOARD_I2C_SCL_PIN | BOARD_I2C_SDA_PIN,
                      GPIO_PIN_SET);

    for (pulses = 0U; pulses < 9U; pulses++) {
        if (HAL_GPIO_ReadPin(BOARD_I2C_GPIO, BOARD_I2C_SDA_PIN)
            == GPIO_PIN_SET) {
            break;
        }
        HAL_GPIO_WritePin(BOARD_I2C_GPIO, BOARD_I2C_SCL_PIN, GPIO_PIN_RESET);
        HAL_Delay(1);
        HAL_GPIO_WritePin(BOARD_I2C_GPIO, BOARD_I2C_SCL_PIN, GPIO_PIN_SET);
        HAL_Delay(1);
    }

    /* Stop condition: SDA low -> high while SCL is high. */
    HAL_GPIO_WritePin(BOARD_I2C_GPIO, BOARD_I2C_SDA_PIN, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(BOARD_I2C_GPIO, BOARD_I2C_SDA_PIN, GPIO_PIN_SET);
    HAL_Delay(1);

    if (!i2c_bus_init()) {
        LOGE("i2c re-init failed");
        return false;
    }
    return HAL_GPIO_ReadPin(BOARD_I2C_GPIO, BOARD_I2C_SDA_PIN) == GPIO_PIN_SET;
}

static bool note_result(const char *what, uint8_t addr7, HAL_StatusTypeDef st)
{
    if (st == HAL_OK) {
        s_streak = 0U;
        return true;
    }

    s_errors++;
    if (s_streak < 1000U) {
        s_streak++;
    }
    LOGE("%s @0x%02x failed (status=%d er=%lu)", what, addr7,
         (int)st, s_errors);

    if (s_streak >= I2C_RECOVER_AFTER) {
        if (bus_recover()) {
            s_streak = 0U;
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* transfers                                                           */
/* ------------------------------------------------------------------ */

bool i2c_bus_write(uint8_t addr7, const uint8_t *data, uint16_t len)
{
    HAL_StatusTypeDef st;

    if (len == 0U) {
        return true;
    }
    st = HAL_I2C_Master_Transmit(&s_i2c, (uint16_t)(addr7 << 1),
                                 (uint8_t *)data, len,
                                 I2C_XFER_TIMEOUT_MS);
    return note_result("wr", addr7, st);
}

bool i2c_bus_read(uint8_t addr7, uint8_t *data, uint16_t len)
{
    HAL_StatusTypeDef st;

    if (len == 0U) {
        return true;
    }
    st = HAL_I2C_Master_Receive(&s_i2c, (uint16_t)(addr7 << 1),
                                data, len, I2C_XFER_TIMEOUT_MS);
    return note_result("rd", addr7, st);
}

bool i2c_bus_write_reg8(uint8_t addr7, uint8_t reg, const uint8_t *data,
                        uint16_t len)
{
    HAL_StatusTypeDef st = HAL_I2C_Mem_Write(
        &s_i2c, (uint16_t)(addr7 << 1), reg, I2C_MEMADD_SIZE_8BIT,
        (uint8_t *)data, len, I2C_XFER_TIMEOUT_MS);

    return note_result("wr8", addr7, st);
}

bool i2c_bus_read_reg8(uint8_t addr7, uint8_t reg, uint8_t *data,
                       uint16_t len)
{
    HAL_StatusTypeDef st = HAL_I2C_Mem_Read(
        &s_i2c, (uint16_t)(addr7 << 1), reg, I2C_MEMADD_SIZE_8BIT,
        data, len, I2C_XFER_TIMEOUT_MS);

    return note_result("rd8", addr7, st);
}

bool i2c_bus_write_reg16(uint8_t addr7, uint16_t reg, const uint8_t *data,
                         uint16_t len)
{
    HAL_StatusTypeDef st = HAL_I2C_Mem_Write(
        &s_i2c, (uint16_t)(addr7 << 1), reg, I2C_MEMADD_SIZE_16BIT,
        (uint8_t *)data, len, I2C_XFER_TIMEOUT_MS);

    return note_result("wr16", addr7, st);
}

bool i2c_bus_read_reg16(uint8_t addr7, uint16_t reg, uint8_t *data,
                        uint16_t len)
{
    HAL_StatusTypeDef st = HAL_I2C_Mem_Read(
        &s_i2c, (uint16_t)(addr7 << 1), reg, I2C_MEMADD_SIZE_16BIT,
        data, len, I2C_XFER_TIMEOUT_MS);

    return note_result("rd16", addr7, st);
}

bool i2c_bus_present(uint8_t addr7)
{
    return HAL_I2C_IsDeviceReady(&s_i2c, (uint16_t)(addr7 << 1), 1U, 5U)
           == HAL_OK;
}

uint32_t i2c_bus_error_count(void)
{
    return s_errors;
}

void i2c_bus_scan(void)
{
    uint8_t found = 0U;

    for (uint8_t addr = 0x08U; addr <= 0x77U; addr++) {
        if (i2c_bus_present(addr)) {
            LOGI("i2c: found device at 0x%02x", addr);
            found++;
        }
    }
    if (found == 0U) {
        LOGE("i2c: no slaves answered (wiring? pull-ups? VCC?)");
    }
}
