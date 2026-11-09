#!/usr/bin/env python3
"""Compares a run_script.sh capture with live_expected.txt.

  live_check.py <script.log> <peer.log>

live_expected.txt holds the decoded values of one full run, each read against the
constants in rpctest.pwn. Any line missing from a later run, any BAD line (a message
that did not use exactly the bits the server sent) or any truncation fails the check.
"""
import os, re, sys

logs = ''.join(open(p, errors='replace').read() for p in sys.argv[1:3])
want = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'live_expected.txt')).read().splitlines()
lines = set(logs.splitlines())
missing = [w for w in want if w and w not in lines]
bad = re.findall(r'^BAD.*$', logs, re.M)
truncated = [s for s in re.findall(r'truncated (\d+)', logs) if s != '0']
for m in missing:
    print('MISSING', m)
for b in bad:
    print(b)
print('%d expected lines, %d missing, %d bad, %d runs with truncation' % (len(want), len(missing), len(bad), len(truncated)))
sys.exit(bool(missing or bad or truncated))
