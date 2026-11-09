#!/usr/bin/env python3
"""Checks the logs of live_run.sh: what the real server said against what the client did.
  python wip/vehicle/live_check.py driver.log watcher.log"""
import re
import sys

drv = open(sys.argv[1], errors='replace').read()
wat = open(sys.argv[2], errors='replace').read()
fails = 0


def check(ok, what):
    global fails
    print(('ok   ' if ok else 'FAIL ') + what)
    fails += not ok


def has(text, pat, what):
    check(re.search(pat, text, re.M) is not None, what)


FIELDS = ('id model x y z qw qx qy qz health c1 c2 plate paint m0 m1 m2 panels doors lights_dmg tyres interior '
          'engine lights locked alarm bonnet d0 d1 siren driver towed vx vy vz tx ty tz').split()
PAT = (r"^EV %s (\d+) model (\d+) at (\S+) (\S+) (\S+) quat (\S+) (\S+) (\S+) (\S+) health (\d+) c (\d+) (\d+) "
       r"plate '(.*?)' paint (-?\d+) mods (\d+) (\d+) (\d+) panels (\w+) doors (\w+) lights (\w+) tyres (\w+) "
       r"interior (\d+) engine (\d+) lights (\d+) locked (\d+) alarm (\d+) door (\d+) (\d+) (\d+) siren (\d+) "
       r"driver (\w+) towed (\w+) vel (\S+) (\S+) (\S+) turn (\S+) (\S+) (\S+)$")


def cars(text, tag):
    return [dict(zip(FIELDS, m)) for m in re.findall(PAT % tag, text, re.M)]


def car(text, tag):
    c = cars(text, tag)
    return c[-1] if c else None


ids = re.search(r'CB ids car (\d+) truck (\d+) trailer (\d+) cop (\d+) bike (\d+) empty (\d+)', drv)
check(ids is not None, 'server gave vehicle ids')
car_id, truck, trailer, cop, bike, empty = ids.groups()

# Stream in
seen = {c['id']: c for c in cars(drv, 'seen')}
c = seen.get(car_id)
check(c and c['c1'] == '6' and c['c2'] == '7' and c['plate'] == 'PS2 CAR', 'car streamed in with colours and plate')
check(c and c['x'] == '1905.00' and c['z'] == '13.85', 'position from stream-in')
c = seen.get(empty)
# Mods load one at a time, so at this moment only the first is certain to be on; 'changed' has the rest.
check(c and c['paint'] == '2' and c['m0'] == '1010', 'paintjob and first mod from stream-in')

# RPCs on the empty car
e = car(drv, 'changed')
check(e and e['c1'] == '44' and e['c2'] == '55', 'ChangeVehicleColor')
check(e and e['m1'] == '1073' and e['m2'] == '1087' and e['m0'] == '0', 'AddVehicleComponent, RemoveVehicleComponent')
check(e and e['panels'] == '31' and e['doors'] == '4000302' and e['lights_dmg'] == '5' and e['tyres'] == '6',
      'UpdateVehicleDamageStatus')
check(e and e['interior'] == '3', 'LinkVehicleToInterior')
check(e and e['engine'] == '1' and e['lights'] == '1' and e['locked'] == '1' and e['alarm'] == '1', 'SetVehicleParamsEx')
check(e and e['bonnet'] == '1', 'bonnet param')
check(e and e['d0'] == '1' and e['d1'] == '0', 'SetVehicleParamsCarDoors')

# Entering and driving
has(drv, r'CB enter %s 0' % car_id, 'OnPlayerEnterVehicle from the enter task')
has(drv, r'CB state 2 from 1 seat 0 car %s' % car_id, 'driver state from 200')
u = re.findall(r'CB update car %s at (\S+) (\S+) (\S+) angle (\S+) health (\d+) quat (\S+) (\S+) (\S+) (\S+)' % car_id, drv)
check(len(u) >= 5, 'server tracked the car')
moving = [x for x in u if x[4] == '850']
check(len(moving) >= 3 and float(moving[0][0]) > float(moving[-1][0]) + 10, 'server saw the car move')
check(all(abs(float(x[3]) - 90) < 0.1 for x in u), 'GetVehicleZAngle reads 90: the wire quaternion is right')
has(drv, r'CB damage %s 3300 0 0 0' % car_id, 'OnVehicleDamageStatusUpdate from 106')
v = car(drv, 'velocity')
check(v and v['vx'] == '0.10' and v['vy'] == '0.20' and v['vz'] == '0.30', 'SetVehicleVelocity')
check(v and v['tx'] == '0.010' and v['tz'] == '0.030', 'SetVehicleAngularVelocity')
h = car(drv, 'health')
check(h and h['health'] == '500', 'SetVehicleHealth')
r = car(drv, 'repaired')
check(r and r['health'] == '1000' and r['panels'] == '0', 'RepairVehicle')
m = car(drv, 'moved')
check(m and m['x'] == '1902.00' and m['y'] == '-2440.00', 'SetVehiclePos')

# Leaving
has(drv, r'CB exit %s' % car_id, 'OnPlayerExitVehicle from the leave task')
has(drv, r'CB state 1 from 2 seat -1 car 0', 'on-foot state once foot sync resumes')

# Unoccupied
un = re.findall(r'CB unoccupied %s seat 0 at \S+ (\S+) \S+' % empty, drv)
check(len(un) >= 3 and float(un[-1]) > float(un[0]), 'OnUnoccupiedVehicleUpdate: the server accepted 209')
w = [float(c['y']) for c in cars(wat, 'watch_empty')]
check(w and max(w) > -2480, 'second client saw the empty car move')

# Truck, trailer
has(drv, r'CB state 2 from 1 seat 0 car %s' % truck, 'PutPlayerInVehicle: driver of the truck')
t = car(drv, 'towing')
check(t and t['towed'] != '0', 'AttachTrailerToVehicle reached the game')
has(drv, r'CB trailer %s' % trailer, 'OnTrailerUpdate: the server accepted 210')
has(drv, r'CB update car %s .* trailer %s' % (truck, trailer), 'GetVehicleTrailer')
d = car(drv, 'towing_done')
check(d and d['towed'] == '0', 'DetachTrailerFromVehicle')
tw = [c['towed'] for c in cars(wat, 'watch_truck')]
check(any(x != '0' for x in tw) and tw[-1] == '0', 'second client saw the trailer attach and detach')

# RemovePlayerFromVehicle
has(drv, r'^EV leave_task 1', 'RemovePlayerFromVehicle ran the leave task')
check(re.findall(r'CB exit (\d+)', drv) == [car_id], 'no 154 after RemovePlayerFromVehicle')

# Passenger
has(drv, r'CB state 3 from 1 seat 1 car %s' % truck, 'passenger state from 211')
has(drv, r'CB unoccupied %s seat 1 ' % truck, 'passenger of a driverless car reports it')
m = re.search(r'EV seats driver (\w+) p0 (\w+) me (\w+)', drv)
check(m and m.group(1) == '0' and m.group(2) == m.group(3), 'seated in passenger seat 0')

# Stream out and back
has(drv, r'CB state 1 from 3 seat -1 car 0', 'SetPlayerPos takes him out of the car')
has(drv, r'^EV in_car 0', 'client agrees he is out')
has(drv, r'^EV gone %s none' % car_id, 'car streamed out when far away')
b = car(drv, 'back')
check(b and b['x'] == '1902.00' and b['y'] == '-2440.00', 'car back where it was left')
has(drv, r'CB facing 90\.00', 'GetPlayerFacingAngle: foot quaternions use the same convention')

# The second client saw the driver
pts = cars(wat, 'watch')
xs = [float(p['x']) for p in pts]
check(xs and min(xs) < 1875, 'second client saw the car driven')
check(any(p['qw'] == '0.707' and p['qz'] == '-0.707' for p in pts), 'second client got the rotation')
has(wat, r'^EV hold \d+ 1', 'second client took the ped from game.c')
peds = re.findall(r'^EV watch_ped \d+ car (\w+)', wat, re.M)
check(any(x != '0' for x in peds), 'second client seated the driver ped')
check(peds and peds[0] == '0', 'ped starts on foot')
check(any(p['panels'] == '3300' for p in pts), 'second client got the damage')
for name, text in (('driver', drv), ('watcher', wat)):
    check('Sanitizer' not in text and 'runtime error' not in text and 'VIOLATION' not in text,
          name + ': no sanitizer or fake-game violation')
print('%d failed' % fails)
sys.exit(fails != 0)
