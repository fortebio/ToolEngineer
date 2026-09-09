#!/usr/bin/env python3
"""
Measure why http://<id>.local/ "often does not open" on a REAL device.

That complaint covers four different failures that look identical in a browser
(a blank page and a spinner), and guessing between them wastes days:

  1. the machine is not on the LAN at all (STA never came up -> SoftAP fallback,
     where mDNS is deliberately NOT announced - see dashboardBegin());
  2. the machine is on the LAN and HTTP works, but nothing answers the mDNS
     query (responder never started, or the AP/phone drops multicast);
  3. mDNS answers, but with a STALE address (DHCP moved the device and the
     client cached the old A record) -> the browser hangs on a dead IP, which is
     exactly the blank-page-with-spinner symptom;
  4. everything answers fine from a PC -> the problem is the client (iOS cache,
     phone on a different SSID/VLAN/guest network, band with mDNS filtering).

This probe separates them. Per round it (a) sends a raw mDNS A query for
<host>.local and listens for answers, (b) asks the OS resolver for the same name
(that is the path the BROWSER takes, and it can fail while the raw query works),
and (c) does a plain HTTP GET /home against whatever address it learned.

HOW TO USE
  Run from a PC on the SAME WiFi as the machine (mDNS is link-local; it does not
  cross subnets or a VPN - disconnect any VPN first):

    python tools/probe_mdns.py rpl03003
    python tools/probe_mdns.py rpl03003 30                    # stop after 30 rounds
    python tools/probe_mdns.py rpl03003 --ip 192.168.0.103    # known IP: also test HTTP
    python tools/probe_mdns.py rpl03003 --iface 192.168.0.50  # multi-homed PC: pick the WiFi NIC

  Then reproduce the failure (leave it running while you retry on the phone).
  Ctrl+C prints the summary and a verdict naming which of the four cases it is.

Only Python stdlib (socket, struct, urllib), like the repo's other probes.
"""
import argparse
import socket
import struct
import time
import urllib.error
import urllib.request

MDNS_ADDR = "224.0.0.251"
MDNS_PORT = 5353
QUERY_WAIT = 2.0   # seconds to collect mDNS answers per round; a healthy responder is <50ms
HTTP_TIMEOUT = 3.0
ROUND_EVERY = 2.0


# ---------------------------------------------------------------- mDNS wire format
def build_query(name, unicast_reply):
    """One standard DNS question for <name> IN A.

    unicast_reply sets the QU bit (top bit of QCLASS, RFC 6762 s5.4): the responder
    then answers straight back to our source port instead of multicasting. That is
    what lets this work from an ephemeral port on a PC where Bonjour already owns
    5353 and we could not join the multicast group.
    """
    qname = b"".join(bytes([len(p)]) + p.encode() for p in name.split(".")) + b"\x00"
    qclass = 0x8001 if unicast_reply else 0x0001
    header = struct.pack(">HHHHHH", 0, 0, 1, 0, 0, 0)
    return header + qname + struct.pack(">HH", 1, qclass)


def read_name(buf, off):
    """Decode a DNS name at off, following compression pointers. Returns (name, next_off)."""
    parts = []
    jumped = False
    end = off
    hops = 0
    while True:
        if off >= len(buf) or hops > 32:
            return ".".join(parts), (end if jumped else off)
        ln = buf[off]
        if ln == 0:
            off += 1
            return ".".join(parts), (end if jumped else off)
        if ln & 0xC0 == 0xC0:  # compression pointer
            if off + 1 >= len(buf):
                return ".".join(parts), len(buf)
            ptr = ((ln & 0x3F) << 8) | buf[off + 1]
            if not jumped:
                end = off + 2
                jumped = True
            off = ptr
            hops += 1
            continue
        off += 1
        parts.append(buf[off:off + ln].decode("utf-8", "replace"))
        off += ln


def parse_a_records(buf, want):
    """Every A record in this packet whose owner name matches <want> (case-insensitive)."""
    out = []
    try:
        _, _, qd, an, ns, ar = struct.unpack(">HHHHHH", buf[:12])
        off = 12
        for _ in range(qd):
            _, off = read_name(buf, off)
            off += 4
        for _ in range(an + ns + ar):
            name, off = read_name(buf, off)
            if off + 10 > len(buf):
                break
            rtype, _rclass, ttl, rdlen = struct.unpack(">HHIH", buf[off:off + 10])
            off += 10
            rdata = buf[off:off + rdlen]
            off += rdlen
            if rtype == 1 and rdlen == 4 and name.lower() == want.lower():
                out.append((socket.inet_ntoa(rdata), ttl))
    except (struct.error, IndexError):
        pass
    return out


def open_socket(iface):
    """Bind 5353 + join the group when we can (we then also see multicast answers);
    otherwise fall back to an ephemeral port and rely on the QU bit."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    joined = False
    try:
        s.bind(("", MDNS_PORT))
        mreq = socket.inet_aton(MDNS_ADDR) + socket.inet_aton(iface or "0.0.0.0")
        s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
        joined = True
    except OSError:
        try:
            s.close()
        except OSError:
            pass
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(("", 0))
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 255)
    if iface:
        s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(iface))
    s.settimeout(0.25)
    return s, joined


def mdns_round(sock, fqdn):
    """Send the query, collect answers for QUERY_WAIT. Returns (answers, elapsed_ms, error)."""
    t0 = time.time()
    for unicast in (True, False):
        try:
            sock.sendto(build_query(fqdn, unicast), (MDNS_ADDR, MDNS_PORT))
        except OSError as e:
            return [], 0.0, "send failed: %s" % e
    seen = []
    deadline = t0 + QUERY_WAIT
    while time.time() < deadline:
        try:
            buf, src = sock.recvfrom(4096)
        except socket.timeout:
            continue
        except OSError:
            break
        for ip, ttl in parse_a_records(buf, fqdn):
            if not any(ip == s[0] for s in seen):
                seen.append((ip, ttl, src[0], (time.time() - t0) * 1000.0))
    return seen, (time.time() - t0) * 1000.0, None


# ---------------------------------------------------------------- OS resolver + HTTP
def os_resolve(fqdn):
    t0 = time.time()
    try:
        infos = socket.getaddrinfo(fqdn, 80, socket.AF_INET, socket.SOCK_STREAM)
        ips = sorted({i[4][0] for i in infos})
        return ips, (time.time() - t0) * 1000.0, None
    except OSError as e:
        return [], (time.time() - t0) * 1000.0, str(e)


def http_home(ip):
    t0 = time.time()
    try:
        with urllib.request.urlopen("http://%s/home" % ip, timeout=HTTP_TIMEOUT) as r:
            r.read(256)
            return True, (time.time() - t0) * 1000.0, "HTTP %s" % r.status
    except urllib.error.HTTPError as e:
        return True, (time.time() - t0) * 1000.0, "HTTP %s" % e.code
    except Exception as e:  # URLError, timeout, connection refused/reset
        return False, (time.time() - t0) * 1000.0, type(e).__name__


# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description="Probe why <id>.local does not open.")
    ap.add_argument("host", help="device hostname, e.g. rpl03003 (the .local is optional)")
    ap.add_argument("rounds", nargs="?", type=int, default=0,
                    help="stop after N rounds (0 = until Ctrl+C)")
    ap.add_argument("--ip", default=None,
                    help="known device IP: HTTP is tested against it even when mDNS is silent")
    ap.add_argument("--iface", default=None, help="local IP of the WiFi NIC (multi-homed PC)")
    args = ap.parse_args()

    host = args.host[:-6] if args.host.lower().endswith(".local") else args.host
    fqdn = host + ".local"

    sock, joined = open_socket(args.iface)
    print("Probing %s  (Ctrl+C for summary)" % fqdn)
    print("  mDNS socket: %s" % ("bound :5353 + joined 224.0.0.251" if joined
                                 else "ephemeral port + QU bit only"))
    print("  Run this on the SAME WiFi as the machine, with any VPN off.\n")
    print("%5s %18s %18s  %s" % ("round", "mDNS", "OS resolve", "HTTP /home"))

    n = 0
    mdns_ok = 0
    os_ok = 0
    http_ok = 0
    http_tried = 0
    ips_mdns = {}
    ips_os = {}
    t_start = time.time()
    try:
        while args.rounds == 0 or n < args.rounds:
            n += 1
            answers, _ms, err = mdns_round(sock, fqdn)
            if answers:
                mdns_ok += 1
                for ip, _ttl, _src, _first_ms in answers:
                    ips_mdns[ip] = ips_mdns.get(ip, 0) + 1
                m_txt = "%s %.0fms" % (answers[0][0], answers[0][3])
            else:
                m_txt = err or "-- no answer --"

            os_ips, os_ms, _os_err = os_resolve(fqdn)
            if os_ips:
                os_ok += 1
                for ip in os_ips:
                    ips_os[ip] = ips_os.get(ip, 0) + 1
                o_txt = "%s %.0fms" % (os_ips[0], os_ms)
            else:
                o_txt = "-- fail --"

            target = (answers[0][0] if answers else None) or \
                     (os_ips[0] if os_ips else None) or args.ip
            if target:
                http_tried += 1
                ok, h_ms, h_txt = http_home(target)
                if ok:
                    http_ok += 1
                h_line = "%-15s %s %.0fms" % (target, h_txt, h_ms)
            else:
                h_line = "(no address to try)"

            print("%5d %18s %18s  %s" % (n, m_txt, o_txt, h_line))
            time.sleep(ROUND_EVERY)
    except KeyboardInterrupt:
        pass
    finally:
        sock.close()

    print("\n" + "=" * 66)
    print("SUMMARY")
    print("  rounds:            %d  over %.0fs" % (n, time.time() - t_start))
    print("  mDNS answered:     %d/%d%s" % (mdns_ok, n,
          ("   addresses: %s" % ips_mdns) if ips_mdns else ""))
    print("  OS resolver OK:    %d/%d%s" % (os_ok, n,
          ("   addresses: %s" % ips_os) if ips_os else ""))
    print("  HTTP /home OK:     %d/%d" % (http_ok, http_tried) if http_tried
          else "  HTTP /home OK:     never tried (no address)")
    print("=" * 66)

    if n == 0:
        return
    all_ips = set(ips_mdns) | set(ips_os) | ({args.ip} if args.ip else set())
    if mdns_ok == 0 and http_ok == 0:
        print("VERDICT (case 1): nothing answers - neither mDNS nor HTTP.\n"
              "  The machine is not on this LAN: STA never joined (it fell back to the SoftAP\n"
              "  'FBT-<id>', where mDNS is not announced at all), or this PC is on a different\n"
              "  SSID/VLAN/subnet. Check the TFT QR screen - it prints the live IP.")
    elif mdns_ok == 0 and http_ok > 0:
        print("VERDICT (case 2): HTTP works but NOTHING answers the mDNS query.\n"
              "  The name is the only broken part. Either the responder never started on the\n"
              "  device (MDNS.begin() runs exactly once, at first network-up, and is never\n"
              "  retried - check the serial log for '[dash] mDNS up ->'), or this network drops\n"
              "  multicast to 224.0.0.251 (AP client isolation / IGMP snooping / guest SSID).\n"
              "  Workaround now: use the IP. Fix: retry + re-announce mDNS in firmware.")
    elif len(all_ips) > 1:
        print("VERDICT (case 3): the name resolved to MORE THAN ONE address %s.\n"
              "  DHCP moved the device and a cached A record still points at the old IP - the\n"
              "  browser then hangs connecting to nothing (blank page + spinner). Confirm by\n"
              "  comparing with the IP printed under the QR code on the TFT."
              % sorted(all_ips))
    elif mdns_ok < n:
        print("VERDICT (case 2b): mDNS answers only %d/%d of the time - INTERMITTENT.\n"
              "  Multicast is being dropped on the path, not in the HTTP server. Usual causes:\n"
              "  AP multicast rate-limiting / band steering (phone on 5GHz, device on 2.4GHz),\n"
              "  or a mesh node that does not bridge mDNS. Use the IP, or give the device a\n"
              "  DHCP reservation on the router." % (mdns_ok, n))
    elif os_ok < n:
        print("VERDICT (case 4): the DEVICE answers every query (%d/%d) but this OS resolver\n"
              "  failed %d time(s). The firmware and the network are fine; the failure is in the\n"
              "  client's resolver/cache. On the phone: toggle WiFi off/on, and make sure it is\n"
              "  on the same SSID (not guest, not cellular)." % (mdns_ok, n, n - os_ok))
    else:
        print("VERDICT: mDNS, OS resolver and HTTP all answered every round from this PC.\n"
              "  The device and this network are healthy. Reproduce again from the PHONE while\n"
              "  this keeps running: if the phone still fails while these lines stay green, the\n"
              "  problem is on the phone side (iOS cache, different SSID/VLAN, private relay).")


if __name__ == "__main__":
    main()
