# FatFs R0.16 from https://elm-chan.org/fsw/ff/ is not in the repository, copy these files from its source folder:
#   ff.c, ffunicode.c  ->  platform/fatfs/Src
#   ff.h, diskio.h     ->  platform/fatfs/Inc
# ffconf.h, diskio.c are ours, ffsystem.c is not needed.

fs = import('fs')

foreach vendor_file : ['Src/ff.c', 'Src/ffunicode.c', 'Inc/ff.h', 'Inc/diskio.h']
  if not fs.exists(vendor_file)
    error('FatFs file missing: platform/fatfs/' + vendor_file + ', see platform/fatfs/meson.build')
  endif
endforeach

all_inc += include_directories('Inc')

vendor_sources += files(
  'Src/ff.c',
  'Src/ffunicode.c',
)

drivers_sources += files(
  'Src/diskio.c',
  'Src/vfs_fat.c',
)
