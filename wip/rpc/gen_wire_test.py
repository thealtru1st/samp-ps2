#!/usr/bin/env python3
"""Generate wire tests straight from the write() methods in open.mp's NetCode.

For each server-to-client struct this walks write() line by line and emits C
that serialises sample values in exactly that order, plus the list of field
widths. test_rpc.c then runs the decoder and compares the widths it actually
read, so a transcription slip in samp_rpc.c cannot hide behind a test written by
the same hand. isDL branches are dropped: we are a 0.3.7 client.

Structs with data-dependent branches or loops are listed in MANUAL and tested by
hand in test_rpc.c.

usage: gen_wire_test.py <ref/openmp-src/Shared/NetCode> <samp_rpc.h> <out.inc>
"""
import glob, os, re, sys

NETCODE, HEADER, OUT = sys.argv[1:4]

# Written by the server's NPC code or never to a client: not ours to decode.
CLIENT_TO_SERVER = {'Invalid', 'PlayerConnect', 'PlayerRequestClass', 'PlayerCommandMessage'}
# 0.3.DL only: the server gates these on the client version.
DL_ONLY = {'ModelRequest', 'ModelUrl', 'DownloadCompleted', 'SetPlayerVirtualWorld'}
# Conditionals or loops that a straight walk cannot follow; see test_rpc.c.
MANUAL = {'PlayerFootSync', 'PlayerVehicleSync', 'PlayerMarkersSync', 'PlayerSpectatorSync',
          'SendPlayerScoresAndPings', 'CreateObject', 'SetPlayerObjectMaterial',
          'SetPlayerAttachedObject', 'PlayerInitMenu'}

ARRAYS = {  # writeArray argument -> element bits, count
    'Span<const uint16_t>(SkillLevel)': (16, 11),
    'Span<const uint32_t>(Weapons)': (32, 3),
    'Span<const uint32_t>(Ammos)': (32, 3),
    'Span<const uint8_t>(VehicleModels)': (8 * 212, 1),
    'Name.data()': (8 * 32, 1),
}
SIMPLE = {'writeUINT8': 8, 'writeINT8': 8, 'writeUINT16': 16, 'writeINT16': 16,
          'writeUINT32': 32, 'writeINT32': 32, 'writeFLOAT': 32, 'writeBIT': 1}
VECS = {'writeVEC2': 2, 'writeVEC3': 3, 'writeVEC4': 4}


# Client-to-server structs whose read() is straight enough to walk. test_rpc.c
# calls the matching encoder and compares the widths it wrote.
C2S = ['OnPlayerClickPlayer', 'OnPlayerEnterVehicle', 'OnPlayerSelectObject', 'OnPlayerDeath',
       'OnPlayerDialogResponse', 'OnPlayerSelectTextDraw', 'SCMEvent', 'PlayerRequestChatMessage',
       'SetVehicleDamageStatus', 'OnPlayerGiveTakeDamage', 'OnPlayerEditAttachedObject',
       'OnPlayerEditObject', 'OnPlayerInteriorChange', 'OnPlayerClickMap', 'OnPlayerPickUpPickup',
       'OnPlayerSelectedMenuRow', 'VehicleDeath', 'OnPlayerExitVehicle', 'OnPlayerCameraTarget',
       'OnPlayerDamageActor', 'PlayerFootSync', 'PlayerVehicleSync', 'PlayerPassengerSync',
       'PlayerUnoccupiedSync', 'PlayerTrailerSync', 'PlayerAimSync', 'PlayerBulletSync',
       'PlayerStatsSync', 'PlayerSpectatorSync', 'PlayerRconCommand']
READS = {'readUINT8': [8], 'readINT8': [8], 'readUINT16': [16], 'readINT16': [16], 'readUINT32': [32],
         'readINT32': [32], 'readFLOAT': [32], 'readBIT': [1], 'readVEC3': [32] * 3,
         'readPosVEC3': [32] * 3, 'readVelVEC3': [32] * 3, 'readVEC4': [32] * 4,
         'readGTAQuat': [32] * 4, 'readCompressedPercentPair': [8, 8],
         'readDynStr8': [8, 24], 'readDynStr32': [32, 24]}


def read_widths(text, name):
    """Widths in read() of a struct. Strings are assumed to be 3 bytes long."""
    m = re.search(r'struct %s : NetworkPacketBase<(\d+), NetworkPacketType::(\w+)' % name, text)
    if not m:
        return None
    b = text.index('bool read(NetworkBitStream& bs)', m.end())
    start = text.index('{', b)
    depth, i = 0, start
    while True:
        depth += (text[i] == '{') - (text[i] == '}')
        i += 1
        if depth == 0:
            break
    widths = [8] if m.group(2) == 'Packet' else []
    for fn in re.findall(r'bs\.(read\w+)\(', text[start:i]):
        widths += READS[fn]
    return widths


def find_structs(text):
    """(name, id, kind, body of write()) for every packet struct."""
    for m in re.finditer(r'struct (\w+) : NetworkPacketBase<(\d+), NetworkPacketType::(\w+)', text):
        start = text.index('{', m.end())
        depth, i = 0, start
        while True:
            depth += (text[i] == '{') - (text[i] == '}')
            i += 1
            if depth == 0:
                break
        body = text[start:i]
        w = re.search(r'void write\(NetworkBitStream& bs\) const\s*\{', body)
        wr = None
        if w:
            depth, j = 1, w.end()
            while depth:
                depth += (body[j] == '{') - (body[j] == '}')
                j += 1
            wr = body[w.end():j - 1]
        yield m.group(1), int(m.group(2)), m.group(3), wr


def strip_dl(body):
    """Remove `if (isDL) ...`, and keep only the taken side of `if (!isDL) {...return;}`."""
    m = re.search(r'if \(!isDL\)\s*\{(.*?)return;\s*\}', body, re.S)
    if m:
        return m.group(1)
    return re.sub(r'if \((?:isDL|IsDL)\)\s*\{[^{}]*\}', '', re.sub(r'if \((?:isDL|IsDL)\)\s*bs\.\w+\([^;]*;', '', body))


def walk(name, body):
    """Field list [(kind, arg)] from a write() body, or None if it branches."""
    body = strip_dl(body)
    fields = []
    # The vehicle mods loop is the one fixed-count loop in a straight message.
    body = re.sub(r'for \(int mod : Mods\)\s*\{[^{}]*\}', 'MODS;', body)
    if re.search(r'\bif\b|\bfor\b|\bwhile\b', body):
        return None
    for stmt in re.findall(r'bs\.(\w+)\((.*?)\);|(MODS);', body, re.S):
        fn, arg, mods = stmt
        if mods:
            fields.extend([('writeUINT8', 'mod')] * 14)
        else:
            fields.append((fn, arg.strip()))
    return fields


def emit(fields, val):
    """C statements to write the fields, and the list of widths a reader sees."""
    code, widths = [], []
    for fn, arg in fields:
        if fn in SIMPLE:
            val[0] += 1
            w = SIMPLE[fn]
            if fn == 'writeBIT':
                code.append('bs_wbool(&b, 1);')
            elif fn == 'writeFLOAT':
                code.append('bs_wf(&b, %d.5f);' % val[0])
            else:
                code.append('bs_w%d(&b, %d);' % (w, val[0] & ((1 << w) - 1) if w < 32 else val[0]))
            widths.append(w)
        elif fn in VECS:
            for _ in range(VECS[fn]):
                val[0] += 1
                code.append('bs_wf(&b, %d.5f);' % val[0])
                widths.append(32)
        elif fn in ('writeDynStr8', 'writeDynStr16', 'writeDynStr32'):
            w = int(fn[-1]) if fn.endswith('8') else int(fn[-2:])
            code.append('bs_w%d(&b, 3); bs_wbytes(&b, "abc", 3);' % w)
            widths += [w, 24]
        elif fn == 'WriteCompressedStr':
            code.append('samp_write_compressed_str(&b, "hi");')
            widths.append(-1)  # bit length is data-dependent; checked by consumption
        elif fn == 'writeArray':
            bits, count = ARRAYS[arg]
            for _ in range(count):
                code.append('wzero(&b, %d);' % bits)
                widths.append(bits)
        elif fn == 'writeGTAQuat' or fn == 'writeCompressedVEC3' or fn == 'writeCompressedPercentPair':
            raise SystemExit('unexpected %s in a straight message' % fn)
        else:
            raise SystemExit('unknown writer ' + fn)
    return code, widths


def main():
    structs = []
    for path in sorted(glob.glob(os.path.join(NETCODE, '*.hpp'))):
        structs += list(find_structs(open(path, encoding='utf-8').read()))

    table = open(HEADER).read()
    rpc_ids = {int(m) for m in re.findall(r'X\((\d+),\s+\w+,', table)}

    with_data = {i for n, i, k, w in structs if w and w.strip()}
    out, seen, manual, missing = [], set(), [], []
    for name, pid, kind, wr in structs:
        if wr is None or name in CLIENT_TO_SERVER or name in DL_ONLY:
            continue
        if not wr.strip() and pid in with_data:
            continue  # an empty client-to-server twin of a real message
        if not wr.strip() and pid not in rpc_ids:
            continue  # client-to-server only: an empty write and no client handler
        if pid not in rpc_ids:
            missing.append('%s %d' % (name, pid))
            continue
        if name in MANUAL:
            manual.append(name)
            seen.add(pid)
            continue
        fields = walk(name, wr)
        if fields is None:
            raise SystemExit('%s branches: add it to MANUAL' % name)
        if kind == 'Packet':
            fields = [f for f in fields if f[1] != 'PacketID']
        code, widths = emit(fields, [pid * 100])
        fn = 'samp_sync_dispatch' if kind == 'Packet' else 'samp_rpc_dispatch'
        out.append('    bs_wr_init(&b, buf, sizeof buf);\n    %s\n    check("%s", %s, %d, &b, (const int[]){%s}, %d);\n' % (
            '\n    '.join(code), name, fn, pid, ', '.join(map(str, widths + [0])), len(widths)))
        seen.add(pid)
    if missing:
        raise SystemExit('no decoder for: ' + ', '.join(missing))
    with open(OUT, 'w') as f:
        f.write('/* generated by gen_wire_test.py from open.mp NetCode */\n')
        f.write('#define GEN_MESSAGES %d\n#define GEN_MANUAL %d\n' % (len(out), len(manual)))
        f.write('static void generated_tests(void)\n{\n    bs_t b;\n')
        f.write('\n'.join(out))
        f.write('}\n')
        alltext = ''.join(open(p, encoding='utf-8').read() for p in sorted(glob.glob(os.path.join(NETCODE, '*.hpp'))))
        for name in C2S:
            w = read_widths(alltext, name)
            f.write('static const int rw_%s[] = {%s};\n#define rn_%s %d\n' % (name, ', '.join(map(str, w)), name, len(w)))
    print('%d structs generated, %d manual, %d ids in samp_rpc.h' % (len(out), len(manual), len(rpc_ids)))
    print('in samp_rpc.h but not written by open.mp:', sorted(rpc_ids - seen))


main()
