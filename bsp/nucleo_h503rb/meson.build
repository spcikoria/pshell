# Board: Nucleo-H503RB (MB1814)
#   MCU      STM32H503RB, Cortex-M33 with FPU, 128 KB flash, 32 KB RAM
#   Clock    24 MHz HSE crystal (X3), SYSCLK 250 MHz from PLL1
#   Console  USART3 PA3/PA4, the ST-LINK virtual COM port
#   SD card  SPI1 on the Arduino header: D13 PA5 SCK, D12 PA6 MISO, D11 PA7 MOSI, D10 PC9 CS
#
# A new board: copy this folder to bsp/<board>, add <board> to the choices in meson_options.txt,
# then change the variables below and the sources in Src. Every variable is checked by bsp/meson.build.
# See "Porting to another board" in README.md.

# Device define of the CMSIS header (stm32h5xx.h selects the device file by it), it also selects
# the startup file platform/CMSIS/Device/ST/<st_device_dir>/Source/Templates/startup_<mcu lower case>.s
# and the linker script linker/<mcu>_app.ld
mcu = 'STM32H503xx'

# STM32 family: the prefix of the HAL/LL file names and of system_<st_family>.c
st_family = 'stm32h5xx'

# CMSIS device folder in platform/CMSIS/Device/ST
st_device_dir = 'STM32H5xx'

# HAL and LL modules compiled from platform/HAL/Src/<st_family>_hal_<module>.c and
# platform/LL/Src/<st_family>_ll_<module>.c, <st_family>_hal.c comes with any HAL module. The code calls only
# CMSIS and the inline LL functions of the LL headers, nothing from these files: a new family can start with
# empty lists. The HAL needs <st_family>_hal_conf.h in the include path (bsp/<board>/Inc).
st_hal_modules = ['cortex', 'dma_ex', 'dma', 'exti', 'flash', 'flash_ex', 'gpio', 'icache', 'pcd_ex', 'pcd',
                  'pwr_ex', 'pwr', 'rcc_ex', 'rcc']
st_ll_modules = ['dma', 'exti', 'gpio', 'pwr', 'rcc', 'spi', 'usb', 'utils']

# FreeRTOS port folder in platform/FreeRTOS/Source/portable, it must match the core:
#   Cortex-M33 without TrustZone  GCC/ARM_CM33_NTZ/non_secure
#   Cortex-M4F / M7               GCC/ARM_CM4F / GCC/ARM_CM7/r0p1
#   Cortex-M0 / M0+               GCC/ARM_CM0
# Check configENABLE_FPU, configENABLE_MPU and configENABLE_TRUSTZONE in app/Inc/FreeRTOSConfig.h too.
freertos_port = 'GCC/ARM_CM33_NTZ/non_secure'

# Compiler and linker flags of the core, keep cpu in cross/arm-none-eabi.txt in line
cpu_args = [
  '-mcpu=cortex-m33',
  '-mthumb',
  '-mfloat-abi=hard',
  '-mfpu=fpv5-sp-d16',
]

# Board headers, e.g. <st_family>_hal_conf.h
all_inc += include_directories('Inc')

# Implementation of bsp/Inc/bsp*.h, linked into the application
bsp_sources = files(
  'Src/bsp.c',
  'Src/bsp_console.c',
  'Src/bsp_pins.c',
  'Src/bsp_sdspi.c',
)
