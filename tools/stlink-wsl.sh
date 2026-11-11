#!/usr/bin/env bash
# Shares the Nucleo's ST-LINK (debugger + VCP) with WSL through usbipd-win
#
#   tools/stlink-wsl.sh            attach once (default)
#   tools/stlink-wsl.sh watch      attach, and re-attach on its own after a replug
#   tools/stlink-wsl.sh detach     give the ST-LINK back to Windows
#   tools/stlink-wsl.sh status     show what Windows and WSL see
#
# Runs from WSL and drives the Windows side through usbipd.exe / powershell.exe.
# Needs usbipd-win on Windows (winget install usbipd). The very first run binds
# the device, which needs admin: accept the UAC prompt once, never again after.
# Set STLINK_PID=xxxx to pick one probe when several ST-LINKs are plugged in.

set -euo pipefail

ST_VID=0483
# ST-LINK V2, V2-1 and V3 product IDs
STLINK_PIDS=${STLINK_PID:-'3748|374a|374b|374d|374e|374f|3752|3753|3754|3757'}

die()  { echo "error: $*" >&2; exit 1; }
warn() { echo "warning: $*" >&2; }

if command -v usbipd.exe >/dev/null; then
  USBIPD=usbipd.exe
elif [ -x "/mnt/c/Program Files/usbipd-win/usbipd.exe" ]; then
  USBIPD="/mnt/c/Program Files/usbipd-win/usbipd.exe"
else
  die "usbipd.exe not found, install it on Windows: winget install usbipd"
fi

if command -v powershell.exe >/dev/null; then
  POWERSHELL=powershell.exe
else
  POWERSHELL=/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe
fi

ps_run() { "$POWERSHELL" -NoProfile -NonInteractive -Command "$1" | tr -d '\r'; }

# ---------------------------------------------------------------- Windows side

# Prints "<busid> <attached|shared|not-shared>" for the first connected ST-LINK
win_find_stlink() {
  "$USBIPD" list | tr -d '\r' | awk -v re="^$ST_VID:($STLINK_PIDS)\$" '
    /^Connected:/ { on = 1; next }
    /^Persisted:/ { on = 0 }
    on && tolower($2) ~ re {
      if ($0 ~ /Attached/)    state = "attached"
      else if ($0 ~ /Shared/) state = "shared"
      else                    state = "not-shared"
      print $1, state
      exit
    }'
}

# Binding is the only step that needs admin, so only this one goes through UAC
win_bind() {
  echo "Windows: binding $1 (one-time, accept the UAC prompt)"
  ps_run "Start-Process usbipd -Verb RunAs -Wait -WindowStyle Hidden -ArgumentList 'bind','--busid','$1'" \
    || die "bind failed, was the UAC prompt declined?"
}

PS_AUTOATTACH="Get-CimInstance Win32_Process | Where-Object { \$_.Name -eq 'usbipd.exe' -and \$_.CommandLine -match 'auto-attach' }"

win_autoattach_running() {
  [ "$(ps_run "@($PS_AUTOATTACH).Count")" != 0 ]
}

# A detached, hidden Windows process, so it outlives this shell and the terminal
win_start_autoattach() {
  ps_run "Start-Process usbipd -WindowStyle Hidden -ArgumentList 'attach','--wsl','--busid','$1','--auto-attach'"
}

win_stop_autoattach() {
  ps_run "$PS_AUTOATTACH | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }"
}

# Finds the probe and binds it if needed, sets BUSID and STATE
win_prepare() {
  BUSID= STATE=
  read -r BUSID STATE < <(win_find_stlink) || true
  [ -n "$BUSID" ] || die "no ST-LINK found on Windows, is the board plugged in?"

  if [ "$STATE" = not-shared ]; then
    win_bind "$BUSID"
    read -r BUSID STATE < <(win_find_stlink) || true
    [ "$STATE" != not-shared ] || die "the ST-LINK is still not shared after bind"
  fi
}

# -------------------------------------------------------------------- WSL side

# Prints the sysfs directory of the ST-LINK once WSL sees it
wsl_find_stlink() {
  local d re="^($STLINK_PIDS)\$"
  for d in /sys/bus/usb/devices/*; do
    [ -f "$d/idVendor" ] && [ "$(cat "$d/idVendor")" = "$ST_VID" ] || continue
    [[ $(cat "$d/idProduct") =~ $re ]] || continue
    echo "$d"
    return 0
  done
  return 1
}

# Waits for the device to show up, then checks the debugger and VCP permissions
wsl_check() {
  local d= node t ttys=() i

  for i in $(seq 50); do
    d=$(wsl_find_stlink) && break
    sleep 0.2
  done
  [ -n "$d" ] || die "attached on Windows, but WSL doesn't see the ST-LINK"

  node=$(printf '/dev/bus/usb/%03d/%03d' "$(cat "$d/busnum")" "$(cat "$d/devnum")")
  echo "WSL: $(cat "$d/product" 2>/dev/null || echo ST-LINK) at $node"
  if [ ! -w "$node" ]; then
    warn "no write access to $node, STM32_Programmer_CLI will fail: copy ST's udev rules"
    warn "(STM32CubeProgrammer/Drivers/rules/*.rules) to /etc/udev/rules.d/, enable systemd"
    warn "in /etc/wsl.conf ([boot] systemd=true), then: sudo udevadm control --reload-rules && sudo udevadm trigger"
  fi

  # cdc_acm creates the VCP a moment after the device itself appears
  for i in $(seq 25); do
    for t in "$d"/*/tty/tty*; do
      [ -e "$t" ] && ttys+=("/dev/${t##*/}")
    done
    [ ${#ttys[@]} -gt 0 ] && break
    sleep 0.2
  done

  if [ ${#ttys[@]} -eq 0 ]; then
    warn "no VCP found, is cdc_acm available? (lsmod | grep cdc_acm)"
    return
  fi
  for t in "${ttys[@]}"; do
    echo "WSL: VCP at $t"
    [ -w "$t" ] || warn "no write access to $t: sudo usermod -aG dialout $USER, then restart WSL"
  done
}

# -------------------------------------------------------------------- commands

cmd_attach() {
  win_prepare
  if [ "$STATE" = attached ]; then
    echo "Windows: $BUSID already attached to WSL"
  else
    echo "Windows: attaching $BUSID to WSL"
    "$USBIPD" attach --wsl --busid "$BUSID"
  fi
  wsl_check
}

cmd_watch() {
  win_prepare
  if win_autoattach_running; then
    echo "Windows: auto-attach already running"
  else
    # auto-attach wants to do the attaching itself
    [ "$STATE" != attached ] || "$USBIPD" detach --busid "$BUSID"
    echo "Windows: starting auto-attach for $BUSID in the background"
    win_start_autoattach "$BUSID"
  fi
  wsl_check
}

cmd_detach() {
  local busid= state=
  if win_autoattach_running; then
    echo "Windows: stopping auto-attach"
    win_stop_autoattach
  fi
  read -r busid state < <(win_find_stlink) || true
  if [ "$state" = attached ]; then
    echo "Windows: detaching $busid, the ST-LINK is back on Windows"
    "$USBIPD" detach --busid "$busid"
  else
    echo "Windows: nothing attached"
  fi
}

cmd_status() {
  local busid= state= d
  read -r busid state < <(win_find_stlink) || true
  echo "Windows: ${busid:-no ST-LINK} ${state:-}"
  if win_autoattach_running; then echo "Windows: auto-attach running"; fi
  if d=$(wsl_find_stlink); then
    echo "WSL: sees $(cat "$d/product" 2>/dev/null || echo ST-LINK)"
    ls /dev/ttyACM* 2>/dev/null || true
  else
    echo "WSL: no ST-LINK"
  fi
}

case "${1:-attach}" in
  attach) cmd_attach ;;
  watch)  cmd_watch ;;
  detach) cmd_detach ;;
  status) cmd_status ;;
  *) die "usage: $0 [attach|watch|detach|status]" ;;
esac
