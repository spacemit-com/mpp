#!/usr/bin/env python3
# Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
# Use of this source code is governed by the MIT license in LICENSE.
"""Exercise automatic CLI discovery, aggregation, reconnection and cleanup."""
import os
import select
import signal
import subprocess
import sys
import time

FIXTURE, CLI = sys.argv[1:3]
fixtures, viewers, peers = [], [], []


class Fixture:
    def __init__(self):
        self.proc = subprocess.Popen([FIXTURE], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, text=True)
        fixtures.append(self)
        self.pid = self.proc.pid
        assert self.reply() == "PID %d" % self.pid

    def reply(self):
        assert select.select([self.proc.stdout], [], [], 5)[0], "fixture stalled"
        line = self.proc.stdout.readline().strip()
        assert line, "fixture exited: %s" % self.proc.poll()
        return line

    def command(self, command):
        self.proc.stdin.write(command + "\n")
        self.proc.stdin.flush()
        return self.reply()

    def stop(self):
        self.proc.communicate("quit\n", timeout=4)
        assert self.proc.returncode == 0


class Viewer:
    def __init__(self, *args):
        self.proc = subprocess.Popen([CLI, *args], stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, bufsize=0)
        viewers.append(self)
        self.out, self.err = b"", b""

    def until(self, predicate, timeout=7):
        deadline = time.monotonic() + timeout
        while not predicate():
            remaining = deadline - time.monotonic()
            assert remaining > 0, ("viewer timed out", self.proc.poll(), self.out, self.err)
            streams = [s for s in (self.proc.stdout, self.proc.stderr) if not s.closed]
            ready, _, _ = select.select(streams, [], [], min(remaining, 0.2))
            for stream in ready:
                data = os.read(stream.fileno(), 65536)
                if stream is self.proc.stdout:
                    self.out += data
                else:
                    self.err += data
            if self.proc.poll() is not None:
                assert predicate(), ("viewer exited", self.proc.returncode, self.out, self.err)

    def attached(self, fixture, count=1):
        marker = ("attached to %d;" % fixture.pid).encode()
        self.until(lambda: self.err.count(marker) >= count)

    def event(self, fixture, tag="vdec", level=5):
        offset = len(self.out)
        assert fixture.command("emit %s %d 1" % (tag, level)) == "OK"
        pid = ("[%d:" % fixture.pid).encode()
        event = ("event-%s-%d-0" % (tag, level)).encode()
        self.until(lambda: any(pid in line and event in line
                               for line in self.out[offset:].splitlines()))


def enabled(fixture, expected):
    deadline = time.monotonic() + 4
    while time.monotonic() < deadline:
        if fixture.command("enabled vdec 5") == str(expected):
            return
        time.sleep(0.02)
    raise AssertionError("wrong DEBUG lease for %d" % fixture.pid)


def cli(*args):
    return subprocess.run([CLI, *args], capture_output=True, text=True, timeout=7)


def peer(mode):
    # Advertise a real same-UID/PID endpoint that rejects or stalls a handshake.
    code = r"""
import os, socket, sys
server = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
server.bind('\0mpp-log.%d.%d' % (os.geteuid(), os.getpid()))
server.listen(16)
print('READY', flush=True)
held = []
while True:
    conn, _ = server.accept()
    conn.recv(4096)
    if sys.argv[1] == 'reject':
        conn.send(b'[mpp-log] too many subscribers\n')
        conn.close()
    else:
        held.append(conn)
"""
    proc = subprocess.Popen([sys.executable, "-u", "-c", code, mode], stdout=subprocess.PIPE)
    peers.append(proc)
    assert select.select([proc.stdout], [], [], 4)[0]
    assert proc.stdout.readline() == b"READY\n"
    return proc


try:
    initial = cli("list")
    assert initial.returncode == 0, initial
    empty = len(initial.stdout.splitlines()) == 1
    if empty:
        result = cli()
        assert result.returncode == 1 and "no accessible MPP processes" in result.stderr, result
    else:
        print("SKIP empty-discovery assertion: other MPP services are already running")
    assert cli("-p", "0").returncode == 2
    assert cli("-m", "invalid").returncode == 2
    viewer = Viewer("-m", "vdec", "-l", "debug", "-f")
    if empty:
        viewer.until(lambda: b"waiting for processes" in viewer.err)
    first, second = Fixture(), Fixture()
    for fixture in (first, second):
        viewer.attached(fixture)
        enabled(fixture, 1)
        assert fixture.command("enabled venc 5") == "0"
        assert fixture.command("emit venc 2 1") == "OK"
        viewer.event(fixture)
    assert b"event-venc-" not in viewer.out
    print("PASS no-PID follow, empty waiting, late startup and module/level filters")

    for fixture in (first, second):
        assert fixture.command("emit vdec 4 3") == "OK"
    history = cli("-m", "vdec", "--tail", "2")
    assert history.returncode == 0, history
    for fixture in (first, second):
        lines = [line for line in history.stdout.splitlines() if "[%d:" % fixture.pid in line]
        assert len(lines) == 2 and all("event-vdec-4-" in line for line in lines), history
    default = cli()
    assert default.returncode == 0, default
    assert all("[%d:" % f.pid in default.stdout for f in (first, second)), default
    selected = cli("-p", str(first.pid), "-m", "vdec", "-n", "1")
    assert selected.returncode == 0 and "[%d:" % first.pid in selected.stdout, selected
    assert "[%d:" % second.pid not in selected.stdout, selected
    print("PASS aggregate snapshots, per-process tail counts, defaults and optional PID filter")

    # More than eight sources exercises discovery-array growth while already polling.
    added = [Fixture() for _ in range(8)]
    for fixture in added:
        viewer.attached(fixture)
        viewer.event(fixture)
    print("PASS simultaneous output from ten processes and dynamic source-list growth")

    second.stop()
    viewer.until(lambda: ("PID %d: target disconnected" % second.pid).encode() in viewer.err)
    assert viewer.proc.poll() is None
    viewer.event(first, "mpp_vdec")
    assert first.command("stop") == "OK"
    viewer.until(lambda: ("PID %d: target disconnected" % first.pid).encode() in viewer.err)
    enabled(first, 0)
    assert first.command("start") == "0"
    viewer.attached(first, count=2)
    enabled(first, 1)
    viewer.event(first)
    print("PASS process exit isolation and reconnection after service restart at the same PID")

    rejected, stalled = peer("reject"), peer("stall")
    viewer.until(lambda: ("PID %d: [mpp-log] too many subscribers" % rejected.pid).encode() in viewer.err)
    viewer.event(first, "mpp_vdec")
    viewer.until(lambda: ("PID %d: subscription timed out" % stalled.pid).encode() in viewer.err)
    viewer.event(added[-1], "mpp_vdec")
    assert viewer.proc.poll() is None
    for proc in (rejected, stalled):
        proc.terminate()
        proc.wait(timeout=4)
    print("PASS rejected and stalled subscriptions do not block healthy sources")

    viewer.proc.send_signal(signal.SIGINT)
    assert viewer.proc.wait(timeout=4) == 0
    live = [first, *added]
    for fixture in live:
        assert fixture.proc.poll() is None
        enabled(fixture, 0)
    print("PASS Ctrl+C releases every DEBUG lease without stopping applications")

    pipe_viewer = Viewer("-m", "vdec", "-l", "debug", "-f")
    for fixture in live:
        pipe_viewer.attached(fixture)
    pipe_viewer.proc.stdout.close()
    assert first.command("emit vdec 5 1") == "OK"
    assert pipe_viewer.proc.wait(timeout=4) == 0
    for fixture in live:
        enabled(fixture, 0)
    print("PASS closed output pipe releases all subscriptions cleanly")
finally:
    for viewer in viewers:
        if viewer.proc.poll() is None:
            viewer.proc.kill()
        viewer.proc.wait(timeout=5)
        for stream in (viewer.proc.stdout, viewer.proc.stderr):
            stream.close()
    for proc in peers:
        if proc.poll() is None:
            proc.kill()
        proc.wait(timeout=5)
        proc.stdout.close()
    for fixture in fixtures:
        if fixture.proc.poll() is None:
            fixture.proc.kill()
            fixture.proc.communicate(timeout=5)
