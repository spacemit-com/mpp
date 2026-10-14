#!/usr/bin/env python3
# Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
# Use of this source code is governed by the MIT license in LICENSE.
"""Exercise log levels and the real library against a private syslog socket."""
import os
from pathlib import Path
import re
import select
import socket
import subprocess
import sys
import tempfile
import time

FIXTURE = sys.argv[1]
temp = tempfile.TemporaryDirectory(prefix="mpp-syslog-")
path = Path(temp.name) / "log"
config = Path(temp.name) / "log-level"
receiver = None
processes = []


def bind_receiver():
    result = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    result.bind(str(path))
    result.settimeout(3)
    return result


class Fixture:
    def __init__(self, level=None, override=None):
        env = dict(os.environ, MPP_SYSLOG_SOCKET=str(path), MPP_LOG_LEVEL_FILE=str(config))
        env.pop("MPP_LOG_LEVEL", None)
        if level is not None:
            env["MPP_LOG_LEVEL"] = level
        args = [FIXTURE] if override is None else [FIXTURE, str(override)]
        self.proc = subprocess.Popen(args, env=env, stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        processes.append(self.proc)
        self.pid = self.proc.pid
        self.initial = int(self.reply().split(" LEVEL ")[1])

    def reply(self):
        assert select.select([self.proc.stdout], [], [], 5)[0], "fixture stalled"
        line = self.proc.stdout.readline().strip()
        assert line, ("fixture exited", self.proc.poll())
        return line

    def command(self, text):
        self.proc.stdin.write(text + "\n")
        self.proc.stdin.flush()
        return self.reply()

    def idle_threads(self):
        assert len(list(Path("/proc/%d/task" % self.pid).iterdir())) == 1

    def stop(self):
        out, err = self.proc.communicate("quit\n", timeout=5)
        assert self.proc.returncode == 0 and not out and not err, (self.proc.returncode, out, err)


def record(fixture, marker, priority):
    data = receiver.recv(4096)
    prefix = ("<%d>mpp[%d]: " % (priority, fixture.pid)).encode()
    assert data.startswith(prefix) and marker in data, data
    assert b"\n" not in data, data
    return data


def drain():
    result = []
    while select.select([receiver], [], [], 0)[0]:
        result.append(receiver.recv(4096))
    return result


def wait_level(fixture, expected):
    deadline = time.monotonic() + 3
    while fixture.command("level") != str(expected):
        assert time.monotonic() < deadline, ("level did not refresh", expected)
        time.sleep(0.02)


def wait_probe(fixture, expected):
    deadline = time.monotonic() + 3
    while True:
        value = int(fixture.command("probe"))
        if value:
            record(fixture, b"argument 1", 15)
        if value == expected:
            return
        assert time.monotonic() < deadline, ("log call did not refresh", expected)
        time.sleep(0.02)


def runtime_config():
    names = ("fatal", "error", "warn", "info", "debug", "verbose", "silent")
    cases = [(name, i) for i, name in enumerate(names, 1)]
    cases += [(str(i), i) for i in range(1, 8)]
    cases += [("  DeBuG\t\n", 5), ("", 3), ("invalid", 3), ("debug extra", 3),
              ("0", 3), ("8", 3), ("12", 3), ("debug\0", 3), (" " * 64, 3)]
    for text, expected in cases:
        config.write_text(text)
        target = Fixture("error", 3)
        assert target.initial == expected, (text, target.initial)
        target.stop()

    config.unlink()
    config.mkdir()
    target = Fixture("warn")
    assert target.initial == 3
    target.stop()
    config.rmdir()
    os.mkfifo(config)
    target = Fixture("warn")
    assert target.initial == 3
    target.stop()
    config.unlink()
    link_target = Path(temp.name) / "linked-level"
    link_target.write_text("debug")
    config.symlink_to(link_target)
    target = Fixture("warn")
    assert target.initial == 3
    target.stop()
    config.unlink()

    config.write_text("debug\n")
    target = Fixture("error", 3)
    other = Fixture("fatal")
    assert target.initial == other.initial == 5
    assert target.command("base 2") == "5"
    wait_probe(target, 1)
    config.write_text("silent\n")
    wait_probe(target, 0)
    wait_level(other, 7)
    config.write_text("debug\n")
    wait_probe(target, 1)
    wait_level(other, 5)
    # Truncation during an echo and malformed input must keep the last value.
    for text in ("", "debug extra"):
        config.write_text(text)
        time.sleep(1.05)
        assert target.command("level") == other.command("level") == "5"
    replacement = Path(temp.name) / "replacement"
    replacement.write_text("warn\n")
    replacement.replace(config)
    wait_level(target, 3)
    wait_level(other, 3)
    config.unlink()
    wait_level(target, 2)
    wait_level(other, 1)
    assert target.command("base 4") == "4"
    target.idle_threads()
    other.idle_threads()
    target.stop()
    other.stop()
    print("PASS file levels, runtime multi-process updates, SILENT recovery, invalid input and removal fallback")


try:
    receiver = bind_receiver()
    cases = [(None, 4), ("fatal", 1), ("error", 2), ("warn", 3), ("info", 4),
             ("DEBUG", 5), ("verbose", 6), ("silent", 7), ("invalid", 4),
             ("", 4), ("0", 4), ("8", 4), ("12", 4), ("4x", 4)]
    cases.extend((str(level), level) for level in range(1, 8))
    for value, expected in cases:
        fixture = Fixture(value)
        assert fixture.initial == expected, (value, fixture.initial)
        fixture.idle_threads()
        fixture.stop()
    for value, override in (("silent", 5), ("debug", 3), ("error", 99)):
        fixture = Fixture(value, override)
        assert fixture.initial == (4 if override == 99 else override)
        fixture.stop()
    print("PASS environment levels, invalid-value fallback and API precedence")

    runtime_config()

    fixture = Fixture()
    fixture.idle_threads()
    assert fixture.command("probe") == "0"
    assert not select.select([receiver], [], [], 0.1)[0]
    for base in range(1, 8):
        assert fixture.command("base %d" % base) == str(base)
        for level, priority in ((1, 10), (2, 11), (3, 12), (4, 14), (5, 15), (6, 15)):
            assert fixture.command("emit vdec %d 1" % level) == "OK"
            if base != 7 and level <= base:
                record(fixture, ("event-vdec-%d-0" % level).encode(), priority)
            else:
                assert not select.select([receiver], [], [], 0.01)[0], (base, level)
    assert fixture.command("base 5") == "5"
    assert fixture.command("probe") == "1"
    record(fixture, b"argument 1", 15)
    assert fixture.command("base 99") == "4"
    assert fixture.command("emit /src/al/vcodec/linlonv5v7_dec.c 4 1") == "OK"
    record(fixture, b"[al]", 14)
    assert fixture.command("long") == "OK"
    assert len(record(fixture, b"[truncated]", 14)) < 1600
    assert fixture.command("multiline") == "OK"
    record(fixture, b"one second third", 14)
    fixture.idle_threads()
    print("PASS runtime levels, SILENT, syslog priorities, errno, disabled arguments and formatting")

    other = Fixture()
    for target in (fixture, other):
        assert target.command("emit sys 4 1") == "OK"
    data = [receiver.recv(4096) for _ in range(2)]
    for target in (fixture, other):
        assert any(("mpp[%d]:" % target.pid).encode() in line for line in data), data
    other.stop()
    assert fixture.command("fork") == "CHILD 0"
    child = receiver.recv(4096)
    assert b"child alive" in child and ("mpp[%d]" % fixture.pid).encode() not in child, child
    assert fixture.command("emit utils 3 1") == "OK"
    record(fixture, b"event-utils-3-0", 12)
    print("PASS process identity and fork")

    receiver.close()
    receiver = None
    path.unlink()
    began = time.monotonic()
    assert fixture.command("burst 5000") == "OK"
    assert time.monotonic() - began < 4, "missing syslog receiver blocked producers"
    receiver = bind_receiver()
    assert fixture.command("emit al 2 1") == "OK"
    record(fixture, b"event-al-2-0", 11)
    record(fixture, b"syslog transport dropped 20000 records", 12)

    began = time.monotonic()
    assert fixture.command("burst 5000") == "OK"
    assert time.monotonic() - began < 4, "full syslog queue blocked producers"
    queued = drain()
    assert queued
    assert all(re.match(rb"<\d+>mpp\[\d+\]: [^\n]+$", line) for line in queued), queued
    assert fixture.command("emit al 2 1") == "OK"
    record(fixture, b"event-al-2-0", 11)
    record(fixture, b"syslog transport dropped ", 12)
    fixture.idle_threads()
    fixture.stop()
    print("PASS concurrent nonblocking sends, loss accounting, restart recovery, thread count and terminal isolation")
finally:
    for process in processes:
        if process.poll() is None:
            process.kill()
        process.communicate(timeout=5)
    if receiver is not None:
        receiver.close()
    temp.cleanup()
