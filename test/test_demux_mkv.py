#!/usr/bin/env python3
"""Generate tiny local fixtures and compare MPP demux packets with FFmpeg.

No camera, network, downloaded data, or root privileges are needed.
"""
import csv
from decimal import Decimal
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def run(args):
    result = subprocess.run([str(value) for value in args],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        sys.stderr.buffer.write(result.stderr)
        result.check_returncode()
    return result


def probe(path):
    return json.loads(run([
        'ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_packets',
        '-show_entries', 'packet=pts_time,size,data_hash', '-show_data_hash', 'sha256',
        '-of', 'json', path]).stdout)['packets']


def main():
    if not shutil.which('ffmpeg') or not shutil.which('ffprobe'):
        print('SKIP: ffmpeg and ffprobe required for generated fixtures')
        return 77
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='mpp-mkv-test-') as name:
        root = Path(name)
        for codec, encoder, extra in (
                ('mjpeg', 'mjpeg', ['-pix_fmt', 'yuvj420p', '-q:v', '2']),
                ('h264', 'libx264', ['-preset', 'ultrafast', '-g', '5', '-bf', '0']),
                ('hevc', 'libx265', ['-preset', 'ultrafast', '-x265-params',
                                   'pools=1:frame-threads=1:keyint=5:bframes=0:log-level=error'])):
            video = root / f'{codec}.mkv'
            run(['ffmpeg', '-nostdin', '-v', 'error', '-f', 'lavfi', '-i',
                 'testsrc2=size=128x96:rate=30', '-f', 'lavfi', '-i',
                 'sine=frequency=440:sample_rate=8000', '-t', '0.5',
                 '-c:v', encoder, '-threads', '1', *extra, '-c:a', 'pcm_s16le', video])
            ledger = root / f'{codec}.csv'
            elementary = root / f'{codec}.es'
            run([binary, video, ledger, elementary])
            with ledger.open() as stream:
                actual = list(csv.DictReader(stream))
            reference = probe(video)
            assert len(actual) == len(reference) == 15
            assert [int(row['pts_us']) for row in actual] == [
                int(Decimal(row['pts_time']) * 1000000) for row in reference]
            if codec == 'mjpeg':
                assert [(int(row['size']), row['sha256']) for row in actual] == [
                    (int(row['size']), row['data_hash'].split(':')[1].lower()) for row in reference]
                uri_ledger = root / 'mjpeg-file-uri.csv'
                run([binary, f'file://{video}', uri_ledger])
                with uri_ledger.open() as stream:
                    assert list(csv.DictReader(stream)) == actual
                # Existing MP4 backend remains byte-identical on the same samples.
                mp4 = root / 'mjpeg.mp4'
                run(['ffmpeg', '-nostdin', '-v', 'error', '-i', video,
                     '-map', '0:v:0', '-c:v', 'copy', mp4])
                run([binary, mp4, root / 'mp4.csv'])
                with (root / 'mp4.csv').open() as stream:
                    mp4_rows = list(csv.DictReader(stream))
                assert [r['sha256'] for r in actual] == [r['sha256'] for r in mp4_rows]
            else:
                expected = root / f'{codec}-reference.es'
                bsf = 'h264_mp4toannexb' if codec == 'h264' else 'hevc_mp4toannexb'
                run(['ffmpeg', '-nostdin', '-v', 'error', '-i', video,
                     '-map', '0:v:0', '-c:v', 'copy', '-bsf:v', bsf, '-f', codec, expected])
                assert elementary.read_bytes() == expected.read_bytes()
            run([binary, video, root / 'seek.csv', root / 'seek.es', '200000'])
            print(f'PASS {codec}: 15 packets, PTS, payload/Annex-B, audio skip, seek/reopen')
        large = root / 'large.MKV'
        run(['ffmpeg', '-nostdin', '-v', 'error', '-f', 'lavfi', '-i',
             'testsrc2=size=1280x720:rate=30', '-vf', 'noise=alls=100:allf=t+u',
             '-frames:v', '2', '-c:v', 'mjpeg', '-threads', '1', '-q:v', '1', large])
        run([binary, large, root / 'large.csv'])
        with (root / 'large.csv').open() as stream:
            actual = list(csv.DictReader(stream))
        reference = probe(large)
        assert max(int(row['size']) for row in actual) > 512 * 1024
        assert [r['sha256'] for r in actual] == [r['data_hash'].split(':')[1].lower() for r in reference]
        for contents in (b'', b'not a Matroska file', b'\x1a\x45\xdf\xa3\xff\x00'):
            invalid = root / 'invalid.mkv'
            invalid.write_bytes(contents)
            result = subprocess.run([binary, invalid, root / 'invalid.csv'],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            assert result.returncode != 0
        audio_only = root / 'audio-only.mkv'
        run(['ffmpeg', '-nostdin', '-v', 'error', '-f', 'lavfi', '-i',
             'sine=frequency=440:sample_rate=8000', '-t', '0.2',
             '-c:a', 'pcm_s16le', audio_only])
        unsupported = root / 'unsupported-video.mkv'
        run(['ffmpeg', '-nostdin', '-v', 'error', '-f', 'lavfi', '-i',
             'testsrc2=size=128x96:rate=30', '-frames:v', '2',
             '-c:v', 'mpeg4', unsupported])
        for video in (audio_only, unsupported):
            result = subprocess.run([binary, video, root / 'rejected.csv'],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            assert result.returncode != 0
        print('PASS large MJPEG, file URI, malformed and unsupported inputs')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
