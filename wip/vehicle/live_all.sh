#!/bin/bash
# From Git Bash at the repo root: compile the gamemode, restart the private server, run the
# two clients and check their logs.
#   bash wip/vehicle/live_all.sh
PROJ=${PROJ:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}
cd $PROJ
python wip/vehicle/make_patch.py || exit 1
Q=tools/ompsrv-run/Server/qawno
(cd wip/vehicle/srv/gamemodes && MSYS_NO_PATHCONV=1 ../../../../$Q/pawncc.exe "-i../../../../$Q/include" "-O1" "-;+" "-(+" ../../vehtest.pwn -ovehtest | tail -2)
powershell -NoProfile -ExecutionPolicy Bypass -File wip/vehicle/srv_restart.ps1 > /dev/null 2>&1 < /dev/null
sleep 3
tools/w wip/vehicle/live_run.sh 7793 | tail -3
OUT=${1:-.}
MSYS_NO_PATHCONV=1 wsl -d Ubuntu -- bash -c 'cat ~/samp-vehicle-test/driver.log' > "$OUT/driver.log"
MSYS_NO_PATHCONV=1 wsl -d Ubuntu -- bash -c 'cat ~/samp-vehicle-test/watcher.log' > "$OUT/watcher.log"
python wip/vehicle/live_check.py "$OUT/driver.log" "$OUT/watcher.log"
