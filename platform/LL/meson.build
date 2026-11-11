# STM32 LL of the board's family: the modules in st_ll_modules (bsp/<board>/meson.build).
# The code needs only the LL headers (inline functions), the sources are compiled for the non-inline functions.
# A new family: copy its LL Inc and Src files (<family>_ll_*) from STM32Cube<family>/Drivers/<family>_HAL_Driver,
# the file names start with the family, so more families fit into these folders.

all_inc += include_directories('Inc')

foreach module : st_ll_modules
  vendor_sources += files('Src' / (st_family + '_ll_' + module + '.c'))
endforeach
