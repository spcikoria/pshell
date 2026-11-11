# Board support: bsp/Inc holds the interface every board implements, bsp/<board> the implementation.
# The board is selected with -Dboard=<board>, the choices are in meson_options.txt.

all_inc += include_directories('Inc')

subdir(get_option('board'))

# Every board has to set these, an error here means its meson.build misses one
foreach board_variable : ['mcu', 'st_family', 'st_device_dir', 'st_hal_modules', 'st_ll_modules',
                          'freertos_port', 'cpu_args', 'bsp_sources']
  if not is_variable(board_variable)
    error('bsp/' + get_option('board') + '/meson.build does not set ' + board_variable)
  endif
endforeach
