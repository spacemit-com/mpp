#!/usr/bin/env python3
# Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
# Use of this source code is governed by the MIT license in LICENSE.
"""Exercise log levels and the real library against a private syslog socket."""
from collections import Counter
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
stderr_files = []


def bind_receiver():
    result = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    result.bind(str(path))
    result.settimeout(3)
    return result


class Fixture:
    def __init__(self, level=None, override=None, syslog_path=None, stderr_target=None):
        env = dict(os.environ, MPP_SYSLOG_SOCKET=str(syslog_path or path), MPP_LOG_LEVEL_FILE=str(config))
        env.pop("MPP_LOG_LEVEL", None)
        if level is not None:
            env["MPP_LOG_LEVEL"] = level
        args = [FIXTURE] if override is None else [FIXTURE, str(override)]
        self.stderr_file = None
        self.stderr_position = 0
        if stderr_target is None:
            self.stderr_file = tempfile.TemporaryFile()
            stderr_files.append(self.stderr_file)
            stderr_target = self.stderr_file
        self.proc = subprocess.Popen(args, env=env, stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=stderr_target, text=True)
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

    def stderr_text(self):
        if self.stderr_file is None:
            return ""
        # pread does not change the offset shared with the writing subprocess.
        fd = self.stderr_file.fileno()
        data = os.pread(fd, os.fstat(fd).st_size - self.stderr_position, self.stderr_position)
        self.stderr_position += len(data)
        return data.decode()

    def idle_threads(self):
        assert len(list(Path("/proc/%d/task" % self.pid).iterdir())) == 1

    def stop(self):
        out, err = self.proc.communicate("quit\n", timeout=5)
        remaining = self.stderr_text()
        assert self.proc.returncode == 0 and not out and not err and not remaining, (
            self.proc.returncode, out, err, remaining[:1024])
        if self.stderr_file is not None:
            self.stderr_file.close()


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


def fallback_checks():
    missing = Path(temp.name) / "missing-socket"
    refused = Path(temp.name) / "refused-socket"
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    sock.bind(str(refused))
    sock.close()
    for endpoint in (missing, refused):
        target = Fixture(syslog_path=endpoint)
        for level in range(1, 6):
            assert target.command("emit sys %d 1" % level) == "OK"
        text = target.stderr_text()
        assert text.count("mpp: ") == 4 and "event-sys-5" not in text, text
        for level in range(1, 5):
            assert "event-sys-%d-0" % level in text, text
        assert target.command("long") == "OK"
        assert "[truncated]" in target.stderr_text()
        assert target.command("multiline") == "OK"
        assert "one second third" in target.stderr_text()
        assert target.command("base 7") == "7"
        assert target.command("emit sys 1 1") == "OK"
        assert not target.stderr_text()
        assert target.command("base 6") == "6"
        for level in range(1, 7):
            assert target.command("emit al %d 25" % level) == "OK"
            lines = target.stderr_text().splitlines()
            assert len(lines) == 25, (level, len(lines))
            for index, line in enumerate(lines):
                assert line.endswith("event-al-%d-%d" % (level, index)), line
        target.stop()

    target = Fixture(syslog_path=missing)
    began = time.monotonic()
    assert target.command("burst 5000") == "OK"
    elapsed = time.monotonic() - began
    assert elapsed < 4, "fallback flood stalled"
    text = target.stderr_text()
    entries = re.findall(r"^mpp: [^\n]+ burst (\d+)$", text, re.M)
    assert len(text.splitlines()) == len(entries) == 20000
    assert Counter(map(int, entries)) == Counter({i: 4 for i in range(5000)})
    for level in (1, 2):
        assert target.command("emit sys %d 1" % level) == "OK"
        assert "event-sys-%d-0" % level in target.stderr_text()
    assert target.command("fork") == "CHILD 0"
    assert "child alive" in target.stderr_text()
    assert target.command("emit sys 4 1") == "OK"
    assert "event-sys-4-0" in target.stderr_text()
    target.stop()

    # A broken pipe must neither terminate the host nor change its signal mask.
    read_fd, write_fd = os.pipe()
    os.close(read_fd)
    try:
        target = Fixture(syslog_path=missing, stderr_target=write_fd)
    finally:
        os.close(write_fd)
    status = Path("/proc/%d/status" % target.pid)
    mask = re.search(r"^SigBlk:.*$", status.read_text(), re.M).group()
    assert target.command("emit sys 2 1") == "OK"
    assert re.search(r"^SigBlk:.*$", status.read_text(), re.M).group() == mask
    target.stop()

    # Respect an application's nonblocking stderr and resume writing after it drains.
    read_fd, write_fd = os.pipe()
    try:
        os.set_blocking(write_fd, False)
        while True:
            try:
                os.write(write_fd, b"x" * 4096)
            except BlockingIOError:
                break
        target = Fixture(syslog_path=missing, stderr_target=write_fd)
        assert target.command("emit sys 2 1") == "OK"
        assert not os.get_blocking(write_fd)
        while select.select([read_fd], [], [], 0)[0]:
            os.read(read_fd, 65536)
        assert target.command("emit sys 2 1") == "OK"
        text = os.read(read_fd, 65536).decode()
        assert len(text.splitlines()) == 1 and "event-sys-2-0" in text, text
        target.stop()
    finally:
        os.close(read_fd)
        os.close(write_fd)
    print("PASS missing/refused syslog, complete stderr fallback, fork, SILENT, errno and broken/full pipes")


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
    fallback_checks()

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
    assert fixture.stderr_text().count("burst ") == 20000
    receiver = bind_receiver()
    assert fixture.command("emit al 2 1") == "OK"
    record(fixture, b"event-al-2-0", 11)
    record(fixture, b"syslog transport dropped 20000 records", 12)
    assert not fixture.stderr_text(), "healthy syslog also wrote to stderr"

    began = time.monotonic()
    assert fixture.command("burst 5000") == "OK"
    assert time.monotonic() - began < 4, "full syslog queue blocked producers"
    fallback = fixture.stderr_text()
    queued = drain()
    assert queued
    assert fallback.count("burst ") + len(queued) == 20000
    assert all(re.match(rb"<\d+>mpp\[\d+\]: [^\n]+$", line) for line in queued), queued
    assert fixture.command("emit al 2 1") == "OK"
    record(fixture, b"event-al-2-0", 11)
    record(fixture, b"syslog transport dropped ", 12)
    assert not fixture.stderr_text(), "healthy syslog also wrote to stderr"
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
    for stream in stderr_files:
        stream.close()
    temp.cleanup()
