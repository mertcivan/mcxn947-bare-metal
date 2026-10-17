#!/usr/bin/env bash
#
# Terminal debug session: LinkServer gdbserver + arm-none-eabi-gdb.
#
#   make debug                 connect, load, reset, break at main
#   make debug GDBCMD=attach   attach to whatever is running, do not reflash
#
# The MCU-Link probe admits one debugger at a time, and a LinkServer left
# behind keeps both the probe and the port. The trap below is the point of
# this script: the server dies with the session, however gdb exits.

set -u

ELF=${ELF:-build/mcxn947-bare-metal.elf}
PORT=${GDBPORT:-3333}
DEVICE=${DEVICE:-MCXN947:FRDM-MCXN947}
LINKSERVER=${LINKSERVER:-/c/nxp/LinkServer_26.6.137/LinkServer.exe}
GDB=${GDB:-arm-none-eabi-gdb}
GDBCMD=${GDBCMD:-load}
LOG=${LOG:-/tmp/linkserver-$PORT.log}

kill_servers() {
    # Git Bash mangles single-slash switches, hence //F and //IM.
    for exe in LinkServer.exe redlinkserv.exe crt_emu_cm_redlink.exe; do
        taskkill //F //IM "$exe" >/dev/null 2>&1 || true
    done
}

if [ ! -f "$ELF" ]; then
    echo "error: $ELF yok - once 'make' calistir." >&2
    exit 1
fi

echo ">> eski debug sunuculari temizleniyor"
kill_servers
sleep 1

echo ">> LinkServer baslatiliyor (port $PORT)"
"$LINKSERVER" gdbserver --gdb-port "$PORT" "$DEVICE" >"$LOG" 2>&1 &
SRV=$!

# However gdb exits - quit, Ctrl-C, crash - the server goes with it.
trap 'echo; echo ">> sunucu kapatiliyor"; kill "$SRV" 2>/dev/null; kill_servers' EXIT INT TERM

echo ">> sunucunun hazir olmasi bekleniyor"
ready=0
for _ in $(seq 1 60); do
    if grep -q "GDB server listening on port" "$LOG" 2>/dev/null; then ready=1; break; fi
    if ! kill -0 "$SRV" 2>/dev/null; then
        echo "error: LinkServer cikti. Log:" >&2; cat "$LOG" >&2; exit 1
    fi
    sleep 0.5
done
if [ "$ready" -eq 0 ]; then
    echo "error: sunucu 30 sn icinde hazir olmadi. Log:" >&2; cat "$LOG" >&2; exit 1
fi
echo ">> hazir"
echo

if [ "$GDBCMD" = "attach" ]; then
    exec "$GDB" \
        -ex "target remote localhost:$PORT" \
        "$ELF"
else
    exec "$GDB" \
        -ex "target remote localhost:$PORT" \
        -ex "load" \
        -ex "monitor reset" \
        -ex "tbreak main" \
        -ex "continue" \
        "$ELF"
fi
