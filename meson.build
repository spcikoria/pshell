# Shell demo on FreeRTOS, the hardware comes from a board support package in bsp/<board>
#
#   meson setup build/debug --buildtype=debug --cross-file cross/arm-none-eabi.txt -Dboard=nucleo_h503rb
#   meson compile -C build/debug
#
# Another board of an existing build directory: meson configure build/debug -Dboard=<board>
# A new board: see "Porting to another board" in README.md, nothing has to change in this file.

project('advanced-shell', 'c',
  default_options: [
    'buildtype=minsize',
    'c_std=gnu11',
    'warning_level=2',
    'b_staticpic=false',
  ],
)

# FreeRTOSConfig.h, pshell_config.h and the other application configs, the subdirs add the rest
all_inc = [include_directories('app/Inc')]

# The board sets: mcu, st_family, st_device_dir, st_hal_modules, st_ll_modules, freertos_port, cpu_args,
# bsp_sources, see bsp/nucleo_h503rb/meson.build
subdir('bsp')

add_project_arguments(cpu_args + [
  '-D' + mcu,
  '-fdata-sections',
  '-ffunction-sections',
  '--specs=nano.specs',
  '--specs=nosys.specs',
], language: 'c')

add_project_link_arguments(cpu_args + [
  '--specs=nano.specs',
  '--specs=nosys.specs',
  '-Wl,--gc-sections',
  '-Wl,--print-memory-usage',
], language: 'c')

subdir('platform')
subdir('app')

objcopy = find_program('arm-none-eabi-objcopy')

custom_target('application.bin',
  input: application_elf,
  output: 'application.bin',
  command: [objcopy, '-O', 'binary', '@INPUT@', '@OUTPUT@'],
  build_by_default: true,
)

programmer = find_program('STM32_Programmer_CLI', required: false)

if programmer.found()
  run_target('flash-application',
    command: [programmer, '-c', 'port=SWD', '-w', application_elf, '-v', '-rst'],
    depends: application_elf,
  )
endif
