# Advanced SHELL for STM32

An interactive command shell for STM32H5 microcontrollers, running on FreeRTOS.
It connects to a serial terminal and runs two kinds of entries: short **commands**, which run inside the shell
task, and **apps**, which take over the terminal until they finish or the user presses Ctrl+C.

The hardware is behind a small board support package (`bsp/`), selected at build time. The default board is the
**Nucleo-H503RB**: the shell talks to the ST-LINK virtual COM port (USART3, PA3/PA4, 921600 8N1).
See [Porting to another board](#porting-to-another-board) for a new one.

* **MCU:** STM32H5 (Arm Cortex-M33), SYSCLK 250 MHz on the Nucleo-H503RB
* **RTOS:** FreeRTOS, statically allocated
* **Drivers:** CMSIS and STM32 LL, only in `bsp/<board>`
* **Build system:** Meson / Ninja

## Shell features

* **Transport independent:** the shell core (`platform/drivers/Src/pshell.c`) only works with stream buffers.
  A transport (UART now, USB CDC or anything else later) feeds received bytes with `PSH_RxData()` /
  `PSH_RxDataFromISR()`, takes output with `PSH_TxData()` / `PSH_TxByteFromISR()` and overrides `PSH_TxStart()`.
* **Line editing:** echo, left/right arrows to move the cursor, insert and backspace/delete anywhere in the line,
  and Ctrl+C to discard the current line.
* **Command history:** the last 10 commands, browsed with the up and down arrow keys.
* **Arguments:** the command line is split into `argc`/`argv`, up to 8 words, like `main()` in C.
* **Commands:** registered with `PSH_Register()`. They run in the shell task and must return quickly.
* **Apps:** registered with `PSH_RegisterApp()`. They run in a separate, lower priority app task and own
  the terminal until they return:
  * `PSH_AppRead()` reads the raw input bytes (no echo, escape sequences passed through) for interactive apps
  * `PSH_AppDelay()` waits and returns early on Ctrl+C
  * `PSH_AppStopRequested()` tells whether the user pressed Ctrl+C
  * after Ctrl+C an app has 200 ms to return, otherwise the shell kills it and recreates the app task
* **Thread-safe output:** `PSH_Printf()`, `PSH_Send()` and `PSH_Write()` share one mutex, so output
  from commands and apps never interleaves.
* **No built-in commands:** the core only runs what is registered. `PSH_GetCommand()` lists the registered
  entries, `help` and `/usr/bin` are built on it outside the shell core.
* **TUI library:** `platform/tui` is a minimal ncurses replacement: cursor, 256 colors, titled frames (at a
  position, or inline in the scrolling output), tabs, scrolling selection lists, toggle items (checkboxes or any
  mark/color pair per state) and key decoding (arrows, Tab, Shift+Tab, Space).
* **Start-up logo:** set with `PSH_SetLogoPrinter()`, printed when the shell starts.

The limits (number of commands, history depth, buffer sizes, app stack, timeouts) are set in
`platform/drivers/Inc/pshell.h`.

## Demo commands and apps

They are registered in `app/Src/sandbox.c`, `app/Src/fs_commands.c` and `app/Src/main.c`.

| Name     | Type    | Description                                                                         |
|----------|---------|-------------------------------------------------------------------------------------|
| `help`   | command | Lists every registered command and app                                              |
| `clear`  | command | Clears the terminal                                                                 |
| `logo`   | command | Prints the start-up logo with the CPU clock                                         |
| `stats`  | command | FreeRTOS run time stats in two frames: uptime and free heap, then per task state, priority, stack size, lowest free stack and CPU usage since the start |
| `echo`   | command | Prints its arguments like Linux `echo`; `echo text > file` or `>> file` writes them to a file |
| `count`  | app     | Prints its arguments, then counts up once per second until Ctrl+C                   |
| `2048`   | app     | The 2048 sliding tile game, drawn with box characters. Arrow keys to play, `q` to quit |
| `globe`  | app     | A rotating colored ASCII Earth, streamed from flash with no frame buffer. `q` to quit |
| `menu`   | app     | TUI demo: tabs (Tab/Shift+Tab), a titled frame, a selection list, checkboxes and switches (Space). `q` to quit |

The run time stats count with TIM2 at 10 kHz, the 32 bit counter wraps after about 5 days.

The terminal must support UTF-8 and ANSI escape sequences (Windows Terminal, PuTTY, minicom, picocom, ...).

## Project structure

| Path        | Content                                                        |
|-------------|----------------------------------------------------------------|
| `app/`      | Application: `main.c`, demo commands and apps, configs (portable) |
| `bsp/Inc/`  | Board interface: `bsp.h`, `bsp_console.h`, `bsp_sdspi.h`, `bsp_pins.h` |
| `bsp/<board>/` | Board implementation and its `meson.build` (MCU, core, vendor files) |
| `platform/` | CMSIS, HAL/LL, FreeRTOS, common helpers, shell and SD card (`drivers`), VFS, FatFs, TUI |
| `linker/`   | Linker scripts per MCU                                         |
| `cross/`    | Meson cross file for `arm-none-eabi`                           |
| `svd/`      | SVD files for the debugger                                     |
| `tools/`    | ST-LINK helper scripts for WSL                                 |
| `.vscode/`  | Debug configurations (Cortex-Debug) and tasks                  |

## Getting started

### Prerequisites

* **Arm GNU Toolchain** (`arm-none-eabi-gcc`) with newlib-nano
  (Ubuntu/WSL: `sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi binutils-arm-none-eabi`)
* **Meson** and **Ninja** (`sudo apt install meson ninja-build`, or `pip install meson ninja`)
* **STM32CubeProgrammer** (`STM32_Programmer_CLI` in `PATH`) for flashing
* A **serial terminal** with UTF-8 and ANSI color support, 921600 8N1

For debugging in VS Code:

* **Cortex-Debug** extension
* **ST-LINK_gdbserver** (from STM32CubeCLT or STM32CubeIDE) in the Windows environment variable
  `stlink_gdbserver`, or the **J-Link GDB server** in `jlink_gdbserver`
* WSL only: `gdb-multiarch` (`sudo apt install gdb-multiarch`)

### Selecting the board

The board is the meson option `board`, the name of a folder in `bsp/` (default `nucleo_h503rb`, the choices
are listed in `meson_options.txt`):
```shell
$ meson configure build/debug -Dboard=nucleo_h503rb
$ meson configure build/release -Dboard=nucleo_h503rb
```
or for new build directories: `./manage.sh clean && BOARD=<board> ./manage.sh init`. The board selects the MCU, the core flags,
the startup file, the linker script, the FreeRTOS port and the vendor files.

### Building

```shell
$ ./manage.sh init      # meson setup build/debug and build/release
$ ./manage.sh debug     # build/debug/app/application.elf, build/debug/application.bin
$ ./manage.sh release   # build/release/...
$ ./manage.sh clean     # removes build/
```

### Flashing

```shell
$ meson compile -C build/debug flash-application
```
or directly:
```shell
$ STM32_Programmer_CLI -c port=SWD -w build/debug/app/application.elf -v -rst
```
The `flash-application` target only exists if `STM32_Programmer_CLI` was found when `./manage.sh init` ran.

### Debugging

`.vscode/launch.json` has configurations for ST-LINK and J-Link, on Windows and on WSL.
The WSL configuration starts the Windows `ST-LINK_gdbserver.exe` through
`tools/stlink-gdbserver-win.sh` and connects to it over `localhost:61234`. It needs WSL mirrored networking
(`networkingMode=mirrored`). Keep the ST-LINK attached to Windows in this case.
`tools/stlink-wsl.sh` is the alternative: it passes the ST-LINK through to WSL with usbipd-win.

## Porting to another board

Only `bsp/<board>` and the vendor files are hardware specific. The application, the shell, the TUI, the file
system and the SD card protocol reach the hardware through four headers in `bsp/Inc`:

| Header          | The board provides                                                                  |
|-----------------|-------------------------------------------------------------------------------------|
| `bsp.h`         | `BSP_Init()` (priority grouping, clocks, caches, sets `SystemCoreClock`), `BSP_Halt()`, the run time stats counter, board and MCU name |
| `bsp_console.h` | `BSP_CON_Init()`, the UART interrupt and `PSH_TxStart()`, see "Transport independent" above |
| `bsp_sdspi.h`   | SPI bus and chip select of the SD card: `BSP_SD_Init/SetSpeed/Select/Exchange()`     |
| `bsp_pins.h`    | Header pins of the `gpio` app: a table of names, mode, level, toggle and ADC         |

### Same family, another board or MCU (e.g. another STM32H5)

1. Copy `bsp/nucleo_h503rb` to `bsp/<board>` and add `<board>` to the choices in `meson_options.txt`.
2. In `bsp/<board>/meson.build` set `mcu` (e.g. `STM32H562xx`). The startup file
   `platform/CMSIS/Device/ST/STM32H5xx/Source/Templates/startup_<mcu>.s` and the linker script
   `linker/<mcu>_app.ld` must exist, add them if not.
3. Change the sources in `bsp/<board>/Src`: the clock tree in `bsp.c` (crystal, PLL, flash wait states),
   the console UART and its pins in `bsp_console.c`, the SD card SPI and CS in `bsp_sdspi.c` and the pin
   table in `bsp_pins.c`. Set `HSE_VALUE` in `bsp/<board>/Inc/stm32h5xx_hal_conf.h`.
4. `meson configure build/debug -Dboard=<board>`, or `./manage.sh clean && BOARD=<board> ./manage.sh init`.

### Another STM32 family (e.g. STM32C5, STM32G4)

Do the steps above, and before them:

1. Vendor files from STM32Cube&lt;family&gt; (or the ST GitHub repositories):
   * `platform/CMSIS/Device/ST/<STM32xxxx>`: device headers, `system_<family>.c`, the GCC startup files
   * `platform/LL/Inc`, `platform/LL/Src`: `<family>_ll_*` (and the HAL files if you compile the HAL),
     the file names start with the family, so they fit next to the H5 files
   * `platform/FreeRTOS/Source/portable/GCC/<port>` if the core is not a Cortex-M33
2. In `bsp/<board>/meson.build`: `st_family` (e.g. `stm32c5xx`), `st_device_dir` (e.g. `STM32C5xx`),
   `st_hal_modules`/`st_ll_modules` (empty lists are fine, the code uses only the inline LL functions),
   `freertos_port` and `cpu_args` (`-mcpu`, FPU flags). Keep `cpu` in `cross/arm-none-eabi.txt` in line.
3. Replace the `stm32h5xx*.h` includes in `bsp/<board>/Src` with the family's headers. The register level
   code (SPI, ADC, clocks) differs between the families, check it against the reference manual.
4. `app/Inc/FreeRTOSConfig.h`: `configENABLE_FPU` (0 without an FPU), `configENABLE_MPU`,
   `configENABLE_TRUSTZONE`. The interrupt priorities come from `__NVIC_PRIO_BITS` of the CMSIS header.

### Also check

* `linker/<mcu>_app.ld`: flash and RAM sizes, the SRAM runner area (see the memory map below)
* `.vscode/launch.json`: `device` and `svdFile` (add the SVD file to `svd/`)
* `manage.sh`: the default `BOARD`
* Flash size: the build prints the memory usage, on a small MCU leave some demo apps out of
  `app/meson.build` and `app/Src/sandbox.c`

## Memory map

The start of SRAM is kept free for the SRAM runner (code payloads loaded and run from RAM, coming later).
The startup code does not initialize this area.

| Region        | STM32H562            | STM32H503            |
|---------------|----------------------|----------------------|
| Flash         | 0x08000000, 2 MB     | 0x08000000, 128 KB   |
| SRAM runner   | 0x20000000, 32 KB    | 0x20000000, 2 KB     |
| RAM           | 0x20008000, 608 KB   | 0x20000800, 30 KB    |
