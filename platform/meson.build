vendor_sources = []
drivers_sources = []

subdir('CMSIS')
subdir('FreeRTOS')
subdir('HAL')
subdir('LL')
subdir('common')
subdir('drivers')
subdir('vfs')
subdir('fatfs')
subdir('tui')

vendor_lib = static_library('vendor',
  vendor_sources,
  include_directories: all_inc,
  override_options: ['warning_level=0'],
)

drivers_lib = static_library('drivers',
  drivers_sources,
  include_directories: all_inc,
)
