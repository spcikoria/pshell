# Application: main, shell commands and apps. Hardware is reached only through bsp/Inc/bsp*.h,
# so nothing has to change here for a new board.

application_sources = files(
  'Src/fs_bin.c',
  'Src/fs_commands.c',
  'Src/fs_tree.c',
  'Src/game2048.c',
  'Src/globe.c',
  'Src/interrupt_handlers.c',
  'Src/main.c',
  'Src/menu.c',
  'Src/pins.c',
  'Src/raycast.c',
  'Src/sandbox.c',
  'Src/sokoban.c',
  'Src/sokoban_levels.c',
  'Src/syscalls.c',
)

# Linker script of the board's MCU, a new MCU needs linker/<mcu>_app.ld
linker_script = files('../linker' / (mcu + '_app.ld'))

# The board sources are linked directly, not from a library: their interrupt handlers and PSH_TxStart()
# override weak symbols, which would not pull an object out of a library
application_elf = executable('application.elf',
  application_sources + bsp_sources,
  include_directories: all_inc,
  link_with: [drivers_lib, vendor_lib],
  link_args: [
    '-T' + (meson.project_source_root() / 'linker' / (mcu + '_app.ld')),
    '-Wl,-Map=application.map',
  ],
  link_depends: linker_script,
)
