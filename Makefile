##########################################################################################################################
# Generic Makefile (based on gcc) - ported to STM32G431 (CANable 2.0)
##########################################################################################################################

######################################
# target
######################################
TARGET = canable2_pcanpro_canfd
TARGET_CRYSTAL ?= 8
TARGET_USB_ID	?= FS
TARGET_VARIANT ?= CANABLE2

#######################################
# paths
#######################################
BUILD_DIR = build

######################################
# source
######################################
C_SOURCES =  \
Src/main.c \
Src/pcanpro_can.c \
Src/pcanpro_led.c \
Src/pcanpro_timestamp.c \
Src/usbd_conf.c \
Src/usb_device.c \
Src/pcanpro_usbd.c \
Src/usbd_desc.c \
Src/system_stm32g4xx.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pcd.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pcd_ex.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_ll_usb.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_rcc.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_rcc_ex.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash_ex.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash_ramfunc.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_gpio.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_dma_ex.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_dma.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pwr.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pwr_ex.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_cortex.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_exti.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_fdcan.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_tim.c \
Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_tim_ex.c \
Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c \
Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c \
Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c \
Src/RTT/SEGGER_RTT.c \
Src/RTT/SEGGER_RTT_printf.c \
$(PROTO)

# ASM sources
ASM_SOURCES =  \
startup_stm32g431xx.s

#######################################
# binaries
#######################################
PREFIX = arm-none-eabi-
ifdef GCC_PATH
CC = $(GCC_PATH)/$(PREFIX)gcc
AS = $(GCC_PATH)/$(PREFIX)gcc -x assembler-with-cpp
CP = $(GCC_PATH)/$(PREFIX)objcopy
SZ = $(GCC_PATH)/$(PREFIX)size
else
CC = $(PREFIX)gcc
AS = $(PREFIX)gcc -x assembler-with-cpp
CP = $(PREFIX)objcopy
SZ = $(PREFIX)size
endif
HEX = $(CP) -O ihex
BIN = $(CP) -O binary -S

#######################################
# CFLAGS
#######################################
# cpu
CPU = -mcpu=cortex-m4

# fpu
FPU = -mfpu=fpv4-sp-d16

# float-abi
FLOAT-ABI = -mfloat-abi=hard

# mcu
MCU = $(CPU) -mthumb $(FPU) $(FLOAT-ABI)

# macros for gcc
AS_DEFS =

# C defines
C_DEFS =  \
-DUSE_HAL_DRIVER \
-DSTM32G431xx

# AS includes
AS_INCLUDES =

# C includes
C_INCLUDES =  \
-ISrc \
-ISrc/RTT \
-IDrivers/STM32G4xx_HAL_Driver/Inc \
-IDrivers/STM32G4xx_HAL_Driver/Inc/Legacy \
-IMiddlewares/ST/STM32_USB_Device_Library/Core/Inc \
-IDrivers/CMSIS/Device/ST/STM32G4xx/Include \
-IDrivers/CMSIS/Include

# compile gcc flags
ASFLAGS = $(MCU) $(AS_DEFS) $(AS_INCLUDES) $(OPT) -Wall -pedantic -fdata-sections -ffunction-sections

CFLAGS = $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -std=c11 -Wall -pedantic -fdata-sections -ffunction-sections $(BOARD_FLAGS)\
-DHSE_VALUE=$(TARGET_CRYSTAL)000000 -DUSB_MODULE_ID=DEVICE_$(TARGET_USB_ID) -D$(TARGET_VARIANT)

ifeq ($(DEBUG), 1)
CFLAGS += -g -gdwarf-2
endif

# Debug build: full debug info, no optimization (for J-Link + Ozone)
ifeq ($(DEBUG), 2)
CFLAGS += -g3 -gdwarf-2 -O0 -fno-inline -fno-omit-frame-pointer
ASFLAGS += -g3 -gdwarf-2
endif

# Generate dependency information
CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"

#######################################
# LDFLAGS
#######################################
LDSCRIPT = STM32G431CBTx_FLASH.ld

LIBS = -lc -lm -lnosys
LIBDIR =
LDFLAGS = $(MCU) -specs=nano.specs -T$(LDSCRIPT) $(LIBDIR) $(LIBS) -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections

# default action: build the CANable 2.0 (single FDCAN1) firmware
all: canable2

# Release build: no logging, -Os. The PEAK driver accepts this cleanly.
canable2:
	$(MAKE) BOARD=canable2 DEBUG=0 OPT=-Os PROTO=Src/pcanpro_fd_protocol.c BOARD_FLAGS='-DPCAN_FD=1 -DINCLUDE_LIN_INTERFACE=0' elf hex bin

release: clean
	$(MAKE) BOARD=canable2 DEBUG=0 OPT=-Os PROTO=Src/pcanpro_fd_protocol.c BOARD_FLAGS='-DPCAN_FD=1 -DINCLUDE_LIN_INTERFACE=0' elf hex bin

# Debug build for J-Link + Ozone: -O0 + full debug info + SEGGER RTT logging.
# RTT log streams over the J-Link probe (Ozone terminal / J-Link RTT Viewer).
debug: clean
	$(MAKE) BOARD=canable2 DEBUG=2 OPT=-O0 PROTO=Src/pcanpro_fd_protocol.c BOARD_FLAGS='-DPCAN_FD=1 -DINCLUDE_LIN_INTERFACE=0 -DRTT_ENABLE=1' elf hex bin

#######################################
# build the application
#######################################
OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

ELF_TARGET = $(BUILD_DIR)/$(TARGET).elf
BIN_TARGET = $(BUILD_DIR)/$(TARGET).bin
HEX_TARGET = $(BUILD_DIR)/$(TARGET).hex

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	$(AS) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(HEX) $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(BIN) $< $@

$(BUILD_DIR):
	mkdir $@

bin: $(BIN_TARGET)

elf: $(ELF_TARGET)

hex: $(HEX_TARGET)

#######################################
# clean up
#######################################
clean:
	-rm -fR $(BUILD_DIR)*

clean_obj:
	-rm -f $(BUILD_DIR)*/*.o $(BUILD_DIR)*/*.d $(BUILD_DIR)*/*.lst

#######################################
# dependencies
#######################################
-include $(wildcard $(BUILD_DIR)/*.d)

# *** EOF ***
