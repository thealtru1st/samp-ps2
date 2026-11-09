#!/bin/bash
# Two scripted players against a PRIVATE server running combat.pwn:
#   tools/w wip/combat/run_live.sh <port>
# Or one player to watch from the game, joined after the PS2 so that it gets id 1:
#   tools/w wip/combat/run_live.sh <port> actor [seconds]
# The server is the caller's to start and stop. Uses the patched tree that run_tests.sh
# leaves in ~/samp-combat-build/tree.
PROJ=${PROJ:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}
set -e
cd $PROJ
PORT=${1:?port of the private server}
[ "$PORT" != 7777 ] || { echo "this needs its own server, not 7777"; exit 1; }
O=$HOME/samp-combat-build; mkdir -p $O
T=$O/live-tree; rm -rf $T; cp -r payload $T
(cd $T && patch -s -p2 --binary < $PROJ/wip/combat/integration.patch)
GW=$(ip route | awk '/default/{print $3; exit}')
gcc -O1 -g -Wall -Wno-unused -Wno-builtin-declaration-mismatch -fno-strict-aliasing -DCG_HOST -DFAKE_LIVE \
    -I$T -Iwip/combat -o $O/live wip/combat/live_combat.c wip/combat/fake.c $T/feat_combat.c \
    $T/feat_combat_game.c wip/combat/glue.c $T/samp.c $T/rak.c $T/bitstream.c $T/authkey.c $T/samp_rpc.c -lm
if [ "$2" = actor ]; then
  exec $O/live $GW $PORT Actor "${3:-300}" actor
fi
if [ "$2" = stepper ]; then
  # every step of the in-game list, with the actor as player 1
  $O/live $GW $PORT PS2_Stepper 62 stepper > $O/stepper.log 2>&1 &
  sleep 1
  $O/live $GW $PORT Actor 58 actor > $O/actor.log 2>&1
  wait
  exit 0
fi
$O/live $GW $PORT PS2_Victim 16 victim > $O/victim.log 2>&1 &
sleep 1
$O/live $GW $PORT PC_Shooter 14 shooter > $O/shooter.log 2>&1
wait
tail -5 $O/victim.log
