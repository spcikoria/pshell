# FreeRTOS kernel with the port of the board's core (freertos_port from bsp/<board>/meson.build).
# A new core: copy the port folder from the FreeRTOS kernel (portable/GCC/<port>) to Source/portable/GCC.
# The ARMv8-M ports have portasm.c next to port.c, the older ones only port.c.

fs = import('fs')

port_dir = 'Source/portable' / freertos_port

if not fs.exists(port_dir / 'port.c')
  error('FreeRTOS port missing: platform/FreeRTOS/' + port_dir + ', see platform/FreeRTOS/meson.build')
endif

all_inc += include_directories(
  'Source/include',
  port_dir,
)

vendor_sources += files(
  'Source/tasks.c',
  'Source/queue.c',
  'Source/list.c',
  'Source/timers.c',
  'Source/event_groups.c',
  'Source/stream_buffer.c',
  port_dir / 'port.c',
  'Source/portable/MemMang/heap_1.c',
)

if fs.exists(port_dir / 'portasm.c')
  vendor_sources += files(port_dir / 'portasm.c')
endif
