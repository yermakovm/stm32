#!/usr/bin/env bash
# Host test for the BME280 driver: same sources the firmware builds,
# against an emulated SPI slave instead of HAL.
set -euo pipefail

cd "$(dirname "$0")/../.."
mkdir -p build/sensor_test

cc -std=c11 -Wall -Wextra -Werror \
    -ffunction-sections -fdata-sections \
    -DSIM_HOST=1 -DSTM32F401xx \
    -Itools/sensor_test -ICore/Inc \
    Core/Src/bme280.c \
    tools/sensor_test/fake_spi.c \
    tools/sensor_test/fake_log.c \
    tools/sensor_test/sensor_test.c \
    -o build/sensor_test/run -lm

./build/sensor_test/run
