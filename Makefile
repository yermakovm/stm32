BUILD_DIR := build
ELF := $(BUILD_DIR)/blinky.elf

all:
	cmake -B $(BUILD_DIR)
	cmake --build $(BUILD_DIR)

flash: $(ELF)
	STM32_Programmer_CLI \
		-c port=SWD \
		-w $(ELF) \
		-v \
		-rst

erase:
	STM32_Programmer_CLI \
		-c port=SWD \
		-e all

info:
	STM32_Programmer_CLI \
		-c port=SWD

read-flash:
	STM32_Programmer_CLI \
		-c port=SWD \
		-r32 0x08000000 8

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all flash erase info read-flash clean
