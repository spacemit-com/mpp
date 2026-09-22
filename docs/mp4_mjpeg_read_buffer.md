# Large MJPEG MP4 sample regression

The MP4 reader used a fixed 512 KiB input array and returned
`ERR_DEMUX_NO_STREAM` for a larger sample. That status also means EOF, so a
valid high-resolution MJPEG recording could appear to end partway through.

The sample buffer now grows geometrically on demand, is reused between reads,
and is freed by `Mp4Demuxer_Close()`. A 64 MiB allocation guard rejects larger
sample-table entries with `ERR_DEMUX_UNSUPPORTED`; an allocation failure
returns `ERR_DEMUX_NOMEM` without losing the previous buffer or advancing the
sample index. This is ordinary heap memory, not a CMA/DMA-BUF allocation.
Packet storage remains demuxer-owned and must not be retained across another
read or close. The fixed H.264/H.265 Annex-B output buffer is not enlarged;
oversized non-JPEG samples are explicitly rejected.

## Scope

This change is based on upstream `2a2dacd0b52c6335ce6485d4cf61f6dd1227c761`.
It does not change public API signatures, SYS/VB layout, decoder input pools,
stream queue limits, camera calibration, timestamp calculation or VIO.

In particular, the low-level MP4 reader's 64 MiB guard is **not** an end-to-end
pipeline guarantee. The upstream bind-mode DEMUX stream pool still has its
independent 1 MiB `MPP_STREAM_MAX_PAYLOAD` limit. This PR does not increase all
producers' DMA allocation sizes as a side effect of fixing file reading.
The real recording used below is below that separate bound.

## Hardware-independent regression

```sh
cmake -S . -B build -DBUILD_TESTS=ON -DBUILD_ROS2_EXAMPLES=OFF
cmake --build build --target test_mp4_read_buffer
ctest --test-dir build -R '^test_mp4_read_buffer$' --output-on-failure
```

The test includes the reader implementation to inspect allocation reuse and
inject allocation failures, following the existing isolated SYS/VDEC test
pattern. It builds its own sample data, without codecs, devices or downloads.

For a standalone host sanitizer check:

```sh
cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wno-unused-parameter -Werror \
  -g -fsanitize=address,undefined -Iinclude test/test_mp4_read_buffer.c \
  -o /tmp/test_mp4_read_buffer
ASAN_OPTIONS=detect_leaks=1 /tmp/test_mp4_read_buffer
```

Coverage includes the exact 512 KiB boundary, 512 KiB + 1 byte, 649,848 and
1,114,788 byte samples, byte-for-byte contents, dimensions/PTS, seek, reuse
without extra allocations, EOF, initial/growth allocation failure with retry,
64 MiB and oversized guards, short reads, repeated close, and small H.264
Annex-B output.

## 2026-09-22 validation

- X86 ASan/UBSan with leak checking: PASS.
- X86-to-K3 Release build with SpacemiT v1.2.4 / GCC 15.2:
  `mpp`, `test_mp4_read_buffer`, `test_sys_stream_ref`, and
  `test_vdec_stream_ref`: PASS.
- K3 10.0.91.119, cores 0-7: five reader-regression runs and the existing SYS
  and VDEC reference-handoff unit tests: PASS. Fault-injection error messages
  in those unit tests are expected; these are not hardware decoder tests.
- Low-level demux of an existing 4000x1200 MJPEG MP4, approximately 45.49 s:
  upstream stopped after 794 packets; the patched library returned all 1,363.
  Packet 795 was 525,556 bytes. Maximum size was 649,848 bytes. The common
  794 packets had identical sizes and PTS; final PTS was 45,459,474 us.
- Patched `libmpp.so` SHA-256:
  `03fcc0de614d8ef3a2221e80bd9bc7b905504a74410a4322de84259d55e336f1`.
  `ldd` was checked to resolve the isolated library's `libmpp.so.1` SONAME,
  not an installed runtime. An initial harness run had an absent SONAME
  symlink, resolved the old runtime and was rejected; the corrected run above
  uses only the isolated library directory.
- Local evidence: `.cross/validation/mpp-mp4-mjpeg-read-buffer-20260922/`
  in the integration workspace (`build.sh`, `run-tests.sh`, logs and ledgers).
- No full camera/VDEC/VIO/LAS2/mapping/DRM rerun on this new upstream SYS ABI.
  No formal board runtime or camera-sdk/UAV gitlink was replaced. Those
  integration and dependency updates require separate validation.
