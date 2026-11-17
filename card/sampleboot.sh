#!/bin/sh
# SampleBoot - start-up script.
# Started once per power-on by autorun/autorun.sh (copied to /run) as its own
# systemd unit. $1 is the "autorun" folder on the SD card.
#
# 1. stops the stock app and runs sampleboot-ui, which shows the carousel and
#    prints the chosen entry ("stock", a folder name in mods/, or
#    "__poweroff" when the user switched the MPC off from the menu);
# 2. runs that folder's start script (the one its autorun.json names, as the
#    firmware would: autorun.sh, launcher.sh, ...), or starts the stock app.
#    Settings files that an OS keeps at the card root (ROOT_FILES) are shown
#    in mods/ too, where it looks for them from its folder. An OS folder with
#    a sampleboot.inplace file runs from autorun/ itself;
# 3. if no app is running GRACE seconds later, starts the stock app.

DIR=${1:?}
B="$DIR/sampleboot"
MODS="$DIR/mods"
RUN=/run/sampleboot
TIMEOUT=10          # seconds without input before the shown entry starts (0 = never)
TIMEOUT_BROWSE=20   # the same, once the encoder has been turned
BEEP=100            # countdown beeps in the last 5 seconds: level 1-100 (scaled by Main Volume), 0 = off
GRACE=60            # seconds a mod gets to start before falling back to stock
ROOT_FILES="mpcqol.conf"   # files OSes keep at the card root and read from "the folder above their own"
SPLASH=dark         # splash screen: dark, light, off, or <name> for splash-<name>.png
SPLASH_TIME=2       # seconds it stays on screen
BOOT_HISTORY=10     # boots kept in boot-history.log (0 = none)
# (the colours in sampleboot.conf are read by sampleboot-ui itself)
WATCH_PROCS="MPC Sample|sample-base|mpcsimple"   # processes that mean "an app is running"
[ -f "$B/sampleboot.conf" ] && . "$B/sampleboot.conf"

# The previous boot's log goes to boot-history.log, which keeps the last
# BOOT_HISTORY boots, oldest first, each one starting with a "=====" line.
# (Rolled now, not at the end of a boot: a boot ends when the MPC is switched
# off, so its last lines are only certain once the next boot starts.)
if [ "$BOOT_HISTORY" -gt 0 ] 2>/dev/null && [ -s "$B/last-boot.log" ]; then
    { cat "$B/boot-history.log" 2>/dev/null; cat "$B/last-boot.log"; echo; } |
        awk -v n="$BOOT_HISTORY" '/^===== / { b++ } { line[NR] = $0; blk[NR] = b }
            END { for (i = 1; i <= NR; i++) if (blk[i] > b - n) print line[i] }' > "$B/boot-history.tmp" &&
        mv -f "$B/boot-history.tmp" "$B/boot-history.log"
fi
exec > "$B/last-boot.log" 2>&1   # log of this boot, readable from a computer
echo "===== $(date -u '+%Y-%m-%d %H:%M:%S') UTC ====="

log() { echo "$(cut -d' ' -f1 /proc/uptime) $*"; }
# The last line of a boot is written to the card at once: the card may be
# read from a computer (SD card access) without a restart.
done_log() { log "$@"; sync; }

stock() {
    [ -x "$RUN/sampleboot-ui" ] && "$RUN/sampleboot-ui" --volume-restore
    done_log "starting the stock Akai app"
    systemctl start ac50.service 2>/dev/null
    exit 0
}

# An app is running if the stock service is active, or if some process holds
# the surface MIDI port (every OS reads the surface, whatever its process is
# called: SampQOL renames the stock binary's threads, for example), or if a
# process from WATCH_PROCS is running.
alive() {
    systemctl is-active --quiet ac50.service 2>/dev/null && { why="ac50.service active"; return 0; }
    who=$("$RUN/sampleboot-ui" --surface-busy 2>/dev/null) && { why="surface port held by $who"; return 0; }
    for c in /proc/[0-9]*/comm; do
        { read -r n < "$c"; } 2>/dev/null || continue
        case "|$WATCH_PROCS|" in *"|$n|"*) why="process $n"; return 0 ;; esac
    done
    return 1
}

log "SampleBoot"
[ -f /etc/os-release ] && grep '^PRETTY_NAME=' /etc/os-release

# The card is exFAT (no exec bit): run the UI from RAM.
mkdir -p "$RUN"
cp "$B/sampleboot-ui" "$RUN/sampleboot-ui" 2>/dev/null && chmod 755 "$RUN/sampleboot-ui" || { log "sampleboot-ui missing"; stock; }
last=$(cat "$B/last" 2>/dev/null)

# Take the screen and the surface from the stock app. The codec volume is
# lowered first, so cutting the stock app's output does not click; the menu
# brings it back once its own silent stream runs.
"$RUN/sampleboot-ui" --volume-down
systemctl stop ac50.service 2>/dev/null
sleep 1
killall "MPC Sample" 2>/dev/null
{ echo 1 > /sys/class/graphics/fb0/blank; echo 0 > /sys/class/graphics/fb0/blank; } 2>/dev/null

id=$("$RUN/sampleboot-ui" --autorun "$DIR" --last "$last" --timeout "$TIMEOUT" \
    --timeout-browse "$TIMEOUT_BROWSE" --beep "$BEEP" \
    --splash "$SPLASH" --splash-time "$SPLASH_TIME")
rc=$?
[ $rc -eq 0 ] && [ -n "$id" ] || { log "sampleboot-ui failed (exit $rc)"; stock; }
if [ "$id" = __poweroff ]; then
    log "switching off"
    sync
    systemctl poweroff 2>/dev/null || poweroff
    exit 0
fi
echo "$id" > "$B/last"
sync

[ "$id" = stock ] && stock
M="$MODS/$id"
S=$("$RUN/sampleboot-ui" --script "$M") || { log "no start script in mods/$id"; stock; }

# Some OSes keep a settings file at the card root and read it from "the
# folder above their own", which is the card root only when they sit in
# autorun/. SampQOL 0.3.0 does so with mpcqol.conf, and turns its 16 USB
# outputs on from it. Each file of ROOT_FILES found at the card root is shown
# in mods/ too, by binding the file itself: the OS reads and writes the one at
# the card root. (Binding the OS's whole folder over autorun/ would also do,
# but OSes that find the card in /proc/mounts, as MPC Simple does, would then
# take autorun/ for the card.) The binding ends at power-off; the first time,
# a copy of the file is left in mods/ to bind onto.
CARD=${DIR%/*}
for f in $ROOT_FILES; do
    [ -f "$CARD/$f" ] || continue
    { [ -f "$MODS/$f" ] || cp "$CARD/$f" "$MODS/$f"; } 2>/dev/null &&
        mount --bind "$CARD/$f" "$MODS/$f" 2>/dev/null &&
        log "$f: the card root's file shown in mods/" ||
        log "$f: could not show the card root's file in mods/"
done

# An OS that works out the card as "the folder above its own" (MPCTRACKER
# passes it to its app as --card) would take autorun/mods/ for the card. Such
# an OS ships an empty file named sampleboot.inplace, or the user adds one:
# its folder is then bound over autorun/, and it runs from where it would be
# without SampleBoot. Not done for every OS: OSes that find the card in
# /proc/mounts, as MPC Simple does, would take the bound autorun/ for the
# card. The binding ends at power-off; this log stays open across it.
if [ -e "$M/sampleboot.inplace" ]; then
    if mount --bind "$M" "$DIR" 2>/dev/null; then
        log "mods/$id runs from autorun/ (sampleboot.inplace)"
        M="$DIR"
    else
        log "mods/$id: could not bind it over autorun/, running it from mods/"
    fi
fi
log "running mods/$id/$S"
/bin/sh "$M/$S" 2>&1
log "autorun of $id returned, waiting for the app"

# Safety net: if no app runs within GRACE seconds, start the stock app.
i=0
while [ $i -lt "$GRACE" ]; do
    sleep 1
    i=$((i + 1))
    [ $i -ge 15 ] && alive && { done_log "app running after $i s ($why)"; exit 0; }
done
alive && { done_log "app running ($why)"; exit 0; }
log "no app after $GRACE s"
stock
