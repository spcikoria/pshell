# STM32 HAL of the board's family: the modules in st_hal_modules (bsp/<board>/meson.build).
# A new family: copy its HAL Inc and Src files from STM32Cube<family>/Drivers/<family>_HAL_Driver here,
# the file names start with the family, so more families fit into these folders.

all_inc += include_directories('Inc')

foreach module : st_hal_modules
  vendor_sources += files('Src' / (st_family + '_hal_' + module + '.c'))
endforeach

if st_hal_modules.length() > 0
  vendor_sources += files('Src' / (st_family + '_hal.c'))
endif
