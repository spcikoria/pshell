# CMSIS core and the device files of the board's family (st_device_dir, st_family, mcu from bsp/<board>).
# A new family: copy its Device/ST/<STM32xxxx> folder from the ST CMSIS device package (STM32Cube<family>),
# with the system file and the GCC startup files in Source/Templates.

device_dir = 'Device/ST' / st_device_dir

all_inc += include_directories('Include', device_dir / 'Include')

vendor_sources += files(
  device_dir / 'Source/Templates' / ('system_' + st_family + '.c'),
  device_dir / 'Source/Templates' / ('startup_' + mcu.to_lower() + '.s'),
)
