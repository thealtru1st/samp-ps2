#!/bin/bash
# One scripted driver on a private vehtest server, for watching from the PS2:
#   tools/w wip/vehicle/bot.sh <port> [seconds]
# Run wip/vehicle/live_run.sh once first, which builds it. Its log prints here.
O=$HOME/samp-vehicle-test
GW=$(ip route | awk '/default/{print $3; exit}')
PORT=${1:?port of the private server}
[ "$PORT" != 7777 ] || { echo "vehtest needs its own server, not 7777"; exit 1; }
exec $O/veh_live $GW $PORT driver "${2:-26}"
