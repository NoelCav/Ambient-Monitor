"""Find Ambient Monitors on the network and update them over the air.

    python scripts/ota.py            build, list monitors, choose which to update
    python scripts/ota.py --list     only list the monitors found
    python scripts/ota.py --all      build and update every monitor found
    python scripts/ota.py --no-build use the last build

Monitors announce an "_arduino._tcp" mDNS service (wifi_manager.cpp startMdns)
with their device ID and firmware build in its TXT records. This sends one
mDNS question for that service and lists the answers, so no device names need
to be known. Standard library only. The OTA password comes from secrets.ini
([ota] password), as for `pio run -e ota -t upload`.
"""
import argparse
import configparser
import os
import socket
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PIO_HOME = os.path.join(os.path.expanduser('~'), '.platformio')
PIO = os.path.join(PIO_HOME, 'penv', 'Scripts' if os.name == 'nt' else 'bin', 'pio')
ESPOTA = os.path.join(PIO_HOME, 'packages', 'framework-arduinoespressif32', 'tools', 'espota.py')
FIRMWARE = os.path.join(ROOT, '.pio', 'build', 'esp32s3', 'firmware.bin')
SERVICE = '_arduino._tcp.local'
MDNS = ('224.0.0.251', 5353)
PTR, TXT, SRV, A = 12, 16, 33, 1


def query(name, qtype):
    labels = b''.join(bytes([len(p)]) + p.encode() for p in name.split('.')) + b'\0'
    return struct.pack('>6H', 0, 0, 1, 0, 0, 0) + labels + struct.pack('>2H', qtype, 1)


def read_name(msg, i):
    """DNS name at offset i (with compression). Returns (name, offset after it)."""
    parts, end = [], None
    while True:
        n = msg[i]
        if n == 0:
            i += 1
            break
        if n & 0xC0 == 0xC0:  # pointer
            if end is None:
                end = i + 2
            i = ((n & 0x3F) << 8) | msg[i + 1]
            continue
        parts.append(msg[i + 1:i + 1 + n].decode(errors='replace'))
        i += 1 + n
    return '.'.join(parts), (end if end is not None else i)


def records(msg):
    """All resource records in a DNS message as (name, type, rdata offset, rdata)."""
    qd, an, ns, ar = struct.unpack('>4H', msg[4:12])
    i = 12
    for _ in range(qd):
        i = read_name(msg, i)[1] + 4
    out = []
    for _ in range(an + ns + ar):
        name, i = read_name(msg, i)
        rtype, _cls, _ttl, rdlen = struct.unpack('>HHIH', msg[i:i + 10])
        i += 10
        out.append((name, rtype, i, msg[i:i + rdlen]))
        i += rdlen
    return out


def discover(seconds=3.0):
    """Ask once for the service; collect PTR/SRV/TXT/A answers. Our source port
    isn't 5353, so responders answer us directly (mDNS "legacy unicast")."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 255)
    sock.settimeout(0.3)
    instances, srv, txt, addr = set(), {}, {}, {}
    deadline = time.time() + seconds
    for _ in range(2):  # twice, in case a packet is lost
        sock.sendto(query(SERVICE, PTR), MDNS)
    while time.time() < deadline:
        try:
            msg, _ = sock.recvfrom(4096)
        except socket.timeout:
            continue
        for name, rtype, off, rdata in records(msg):
            if rtype == PTR and name == SERVICE:
                instances.add(read_name(msg, off)[0])
            elif rtype == SRV:
                srv[name] = (struct.unpack('>H', rdata[4:6])[0], read_name(msg, off + 6)[0])
            elif rtype == TXT:
                kv, j = {}, 0
                while j < len(rdata):
                    s = rdata[j + 1:j + 1 + rdata[j]].decode(errors='replace')
                    j += 1 + rdata[j]
                    if '=' in s:
                        k, v = s.split('=', 1)
                        kv[k] = v
                txt[name] = kv
            elif rtype == A and len(rdata) == 4:
                addr[name] = socket.inet_ntoa(rdata)
    found = []
    for inst in sorted(instances):
        port, host = srv.get(inst, (3232, inst.split('.')[0] + '.local'))
        ip = addr.get(host)
        if not ip:
            try:
                ip = socket.gethostbyname(host)
            except OSError:
                continue
        info = txt.get(inst, {})
        found.append({'name': info.get('device_id', host.replace('.local', '')), 'ip': ip, 'port': port,
                      'build': info.get('build', '?')})
    return found


def ota_password():
    cfg = configparser.ConfigParser()
    cfg.read(os.path.join(ROOT, 'secrets.ini'))
    if not cfg.has_option('ota', 'password'):
        sys.exit('secrets.ini has no [ota] password (see secrets.ini.example)')
    return cfg.get('ota', 'password')


def choose(found):
    while True:
        ans = input('Update which? numbers (e.g. 1,3), "all", or Enter to cancel: ').strip().lower()
        if not ans:
            return []
        if ans == 'all':
            return found
        try:
            nums = [int(x) for x in ans.replace(' ', '').split(',')]
        except ValueError:
            nums = []
        if nums and all(1 <= n <= len(found) for n in nums):
            return [found[n - 1] for n in nums]
        print('  not a valid choice')


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--list', action='store_true', help='only list monitors')
    ap.add_argument('--all', action='store_true', help='update every monitor found')
    ap.add_argument('--no-build', action='store_true', help='use the last build')
    args = ap.parse_args()

    print('Looking for monitors...')
    found = discover()
    if not found:
        sys.exit('No monitors found. Same network? (Or use: pio run -e ota -t upload)')
    for n, d in enumerate(found, 1):
        print('  %d) %-24s %-15s firmware %s' % (n, d['name'], d['ip'], d['build']))
    if args.list:
        return
    targets = found if args.all else choose(found)
    if not targets:
        return

    if not args.no_build:
        subprocess.run([PIO, 'run', '-e', 'esp32s3', '-d', ROOT], check=True)
    password = ota_password()
    failed = []
    for d in targets:
        print('\nUpdating %s (%s)...' % (d['name'], d['ip']))
        r = subprocess.run([sys.executable, ESPOTA, '-i', d['ip'], '-p', str(d['port']), '-a', password,
                            '-f', FIRMWARE, '-r'])
        if r.returncode:
            failed.append(d['name'])
    print('\nDone.' if not failed else '\nFailed: ' + ', '.join(failed))
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
