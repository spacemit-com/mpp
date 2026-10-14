#!/usr/bin/env python3
# Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
# Use of this source code is governed by the MIT license in LICENSE.
"""Hardware-independent live log integration tests, including the actual CLI."""
import os
import select
import signal
import socket
import struct
import subprocess
import sys
import time

FIXTURE, CLI = sys.argv[1:3]
READY = b"@mpp-log ready\n"
END = b"@mpp-log end\n"
VDEC, VENC, ALL = 1 << 5, 1 << 6, (1 << 14) - 1
process = subprocess.Popen([FIXTURE], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
connections = []
viewers = []


def reply(timeout=10):
    assert select.select([process.stdout], [], [], timeout)[0], "fixture stalled"
    line = process.stdout.readline().strip()
    assert line, "fixture exited: %s" % process.poll()
    return line


def command(text, timeout=10):
    process.stdin.write(text + "\n")
    process.stdin.flush()
    return reply(timeout)


def raw_connect(pid=None):
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    sock.settimeout(3)
    sock.connect("\0mpp-log.%d.%d" % (os.geteuid(), pid or process.pid))
    connections.append(sock)
    return sock


def subscribe(level=4, modules=ALL, tail=0, follow=1):
    sock = raw_connect()
    sock.send(struct.pack("=IIIII", 1, level, modules, tail, follow))
    assert sock.recv(4096) == READY
    return sock


def until(sock, marker, timeout=4):
    deadline = time.monotonic() + timeout
    records = []
    while time.monotonic() < deadline:
        sock.settimeout(max(0.01, deadline - time.monotonic()))
        record = sock.recv(4096)
        assert record, "subscription unexpectedly closed"
        records.append(record)
        if marker in record:
            return records
    raise AssertionError("missing record: %r" % marker)


def enabled(tag, level, expected):
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        if command("enabled %s %d" % (tag, level)) == str(expected):
            return
        time.sleep(0.01)
    raise AssertionError("wrong effective level for %s/%s" % (tag, level))


def cli(*args):
    return subprocess.run([CLI, "-p", str(process.pid), *args], capture_output=True, text=True, timeout=5)


try:
    assert reply() == "PID %d" % process.pid
    listed = subprocess.check_output([CLI, "list"], text=True, timeout=5)
    assert str(process.pid) in listed
    assert cli("-m", "invalid").returncode == 2
    assert cli("--tail", "513").returncode == 2
    for mode in ("demux", "mux"):
        standalone = subprocess.Popen([FIXTURE, mode], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        try:
            assert select.select([standalone.stdout], [], [], 4)[0]
            assert standalone.stdout.readline().strip() == "PID %d" % standalone.pid
            result = subprocess.run([CLI, "-p", str(standalone.pid), "-m", mode, "-n", "1"],
                                    capture_output=True, text=True, timeout=4)
            assert result.returncode == 0 and mode in result.stdout, result
            standalone.communicate("quit\n", timeout=4)
            assert standalone.returncode == 0
        finally:
            if standalone.poll() is None:
                standalone.kill()
                standalone.communicate(timeout=4)
    print("PASS level getter, disabled arguments, errno, discovery and standalone DEMUX/MUX")

    assert command("emit vdec 4 3") == "OK"
    assert command("emit venc 4 3") == "OK"
    history = cli("-m", "vdec", "--tail", "3")
    assert history.returncode == 0, history.stderr
    lines = history.stdout.splitlines()
    assert len(lines) == 3 and all("event-vdec-4-" in line for line in lines), lines
    assert all("venc" not in line for line in lines)
    assert command("long") == "OK"
    long_line = cli("-m", "vdec", "--tail", "1").stdout
    assert "LONG" in long_line and "[truncated]" in long_line and len(long_line) < 1536
    print("PASS filtered history, finite snapshot and bounded long records")

    first = subscribe(5, VDEC)
    second = subscribe(6, VDEC)
    enabled("vdec", 6, 1)
    enabled("venc", 5, 0)
    enabled("/src/mpi/vdec/vdec.c", 5, 1)
    enabled("mpp_vdec", 5, 1)
    assert command("emit vdec 5 1") == "OK"
    assert command("emit venc 5 1") == "OK"
    assert b"event-vdec-5-0" in b"".join(until(first, b"event-vdec-5-0"))
    second.close()
    enabled("vdec", 6, 0)
    enabled("vdec", 5, 1)
    assert command("fork") == "CHILD 0"
    enabled("vdec", 5, 1)  # Child cleanup must not alter the parent's service.
    first.close()
    enabled("vdec", 5, 0)
    assert command("base 7") == "7"
    enabled("vdec", 4, 0)
    lease = subscribe(5, VDEC)
    enabled("vdec", 5, 1)
    lease.close()
    enabled("vdec", 4, 0)
    assert command("base 4") == "4"
    print("PASS independent DEBUG leases, module isolation, SILENT baseline and fork lifecycle")

    errors = subscribe(2, VDEC)
    assert command("emit vdec 4 1") == "OK"
    assert command("emit vdec 2 1") == "OK"
    received = b"".join(until(errors, b"event-vdec-2-0"))
    assert b"event-vdec-4-" not in received
    errors.close()

    invalid = raw_connect()
    invalid.send(struct.pack("=IIIII", 99, 4, ALL, 0, 1))
    assert b"invalid" in invalid.recv(4096)
    invalid.close()
    oversized = raw_connect()
    oversized.send(struct.pack("=IIIII", 1, 4, ALL, 0, 1) + b"extra")
    assert b"invalid" in oversized.recv(4096)
    oversized.close()
    idle = raw_connect()
    probe = subscribe()
    probe.close()
    idle.settimeout(4)
    assert idle.recv(4096) == b""
    idle.close()
    print("PASS severity filter, malformed packets and idle-client timeout")

    slow = subscribe(4, VDEC)
    began = time.monotonic()
    assert command("burst 5000", timeout=15) == "OK"
    elapsed = time.monotonic() - began
    assert elapsed < 15, elapsed
    # An independent snapshot must work even while the first reader is stalled.
    assert cli("-m", "vdec", "--tail", "1").returncode == 0
    assert b"dropped" in b"".join(until(slow, b"dropped", timeout=5))
    slow.close()
    print("PASS four concurrent producers and stalled-reader isolation (%.2fs)" % elapsed)

    viewer = subprocess.Popen([CLI, "-p", str(process.pid), "-m", "vdec", "-l", "debug", "-f"],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    viewers.append(viewer)
    assert select.select([viewer.stderr], [], [], 4)[0]
    assert "attached" in viewer.stderr.readline()
    enabled("vdec", 5, 1)
    assert command("emit vdec 5 1") == "OK"
    assert select.select([viewer.stdout], [], [], 4)[0]
    assert "event-vdec-5-0" in viewer.stdout.readline()
    viewer.send_signal(signal.SIGINT)
    assert viewer.wait(timeout=4) == 0
    enabled("vdec", 5, 0)
    assert process.poll() is None
    print("PASS real CLI foreground output and Ctrl+C without stopping MPP")

    # Repeated startup/shutdown must release abstract sockets and thread/fd resources.
    for _ in range(3):
        assert command("stop") == "OK"
        assert cli("-n", "0").returncode != 0
        assert command("start") == "0"
        assert cli("-n", "0").returncode == 0
    active = subscribe(5, VDEC)
    assert command("stop") == "OK"
    active.settimeout(3)
    assert active.recv(4096) == b""
    active.close()
    enabled("vdec", 5, 0)
    assert command("start") == "0"
    print("PASS disconnect cleanup, repeated service lifecycle and socket release")

    process.stdin.write("quit\n")
    process.stdin.flush()
    assert process.wait(timeout=5) == 0
finally:
    for sock in connections:
        sock.close()
    for viewer in viewers:
        if viewer.poll() is None:
            viewer.kill()
        viewer.communicate(timeout=5)
    if process.poll() is None:
        process.kill()
    process.communicate(timeout=5)
