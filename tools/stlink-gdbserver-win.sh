#!/usr/bin/env bash
# Starts the Windows ST-LINK_gdbserver.exe from WSL, run by the "ST-LINK gdbserver (Windows)"
# task (.vscode/tasks.json) for the "[WSL, ST-Link]" launch configuration.
#
# The exe comes from the Windows variable stlink_gdbserver (the exe or its folder), the same
# one the [Windows, ...] configurations use. Adds -cp for the STM32CubeProgrammer next to it
# and turns Linux paths in the arguments into Windows paths.

set -euo pipefail

die() { echo "error: $*" >&2; exit 1; }

# Read from the registry: processes started from WSL only see the environment WSL started with
win_env() {
  (cd /mnt/c && powershell.exe -NoProfile -NonInteractive -Command "
    \$v = [Environment]::GetEnvironmentVariable('$1', 'User')
    if (-not \$v) { \$v = [Environment]::GetEnvironmentVariable('$1', 'Machine') }
    if (\$v) { [Environment]::ExpandEnvironmentVariables(\$v) }" | tr -d '\r')
}

exe=$(win_env stlink_gdbserver)
[ -n "$exe" ] || die "Windows variable stlink_gdbserver is not set"
exe=$(wslpath -u "$exe")
[ -d "$exe" ] && exe=${exe%/}/ST-LINK_gdbserver.exe
[ -f "$exe" ] || die "ST-LINK_gdbserver.exe not found: $exe"
exe_dir=$(dirname "$exe")

# STM32CubeCLT layout, STM32CubeIDE plugin layout, standalone STM32CubeProgrammer
cp_dir=
for d in "$exe_dir"/../../STM32CubeProgrammer/bin \
         "$exe_dir"/../../../com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_*/tools/bin \
         "/mnt/c/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin"; do
  if [ -d "$d" ]; then cp_dir=$(realpath "$d"); break; fi
done
[ -n "$cp_dir" ] || die "STM32CubeProgrammer not found next to $exe"

args=()
for a in "$@"; do
  if [[ $a == /* ]] && [ -e "$a" ]; then a=$(wslpath -w "$a"); fi
  args+=("$a")
done
args+=(-cp "$(wslpath -w "$cp_dir")")

echo "Windows: $(wslpath -w "$exe") ${args[*]}"
cd "$exe_dir"
exec "$exe" "${args[@]}"
