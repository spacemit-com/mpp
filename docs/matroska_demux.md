# Optional Matroska (MKV) input

Build with `-DDEMUX_BUILD_MKV=ON` to read local `.mkv` files through the same
`Demux_*` context API and `DEMUX_*` channel API as existing file inputs. The
option defaults to OFF, preserving the existing dependency set. When enabled,
install the target's `libavformat-dev`, `libavcodec-dev` and `libavutil-dev`
packages, or provide their headers and libraries in the cross-compilation
sysroot. Missing dependencies fail configuration explicitly.

The optional backend uses FFmpeg's Matroska demuxer; it does not call a shell
command or instantiate a pixel decoder/encoder. Existing MP4/TS/FLV/network
backends are unchanged. No public packet structure or callback signature changes.

## Data and ownership

- MJPEG packets are passed through byte-for-byte, including embedded camera
  metadata. Decoding still belongs to MPP VDEC.
- H.264 and H.265 use FFmpeg bitstream filters to produce Annex-B packets and
  parameter sets for the existing MPP decoder. This is not transcoding.
- The selected video stream is published; audio and other streams are skipped.
  Other video codecs are rejected with `ERR_DEMUX_UNSUPPORTED`.
- An `AVPacket` retains the output allocation until the next read, seek or close.
  Callbacks must not retain its pointer. Existing SYS bind ownership is unchanged.
  The demuxer itself makes no DMA/CMA allocations and does not retain the whole
  video or an unbounded application-side packet queue.
- Packet PTS is rescaled from the container time base to microseconds, not
  synthesized from integer FPS. DTS is used only if PTS is absent. Missing or
  negative timestamps are rejected because the current MPP PTS ABI is unsigned.
  The camera's embedded exposure/IMU clock is not changed by demuxing.
- Width/height and codec come from Matroska TrackEntry. FPS can be zero if the
  file does not advertise it; channel pacing still uses packet PTS. Duration is
  reported in microseconds when available, otherwise zero.
- Seek selects an earlier/equal keyframe and flushes filter state. Reopen and
  repeated close are supported. EOF does not reconnect/replay the file.
- Only regular local files (paths or `file:///...`) are accepted. Network URLs
  and FIFOs are not handled by this backend.

## Tests

```sh
cmake -S . -B build -DDEMUX_BUILD_MKV=ON -DBUILD_ROS2_EXAMPLES=OFF
cmake --build build --parallel --target test_demux_mkv test_mp4_read_buffer
ctest --test-dir build -R '^(test_demux_mkv|test_mp4_read_buffer)$' --output-on-failure
```

The Python integration test generates short fixtures locally with `ffmpeg`
(MJPEG, libx264 and libx265 encoders). It compares packet PTS and MJPEG payload
hashes with `ffprobe`, compares H.264/H.265 Annex-B output with FFmpeg, and checks
audio skipping, EOF, seek/reopen, file URI input, large MJPEG packets, invalid
headers, audio-only files and unsupported video codecs. No
download, camera or root privilege is required. CTest skips this fixture test
when the FFmpeg CLI tools are unavailable; the libraries alone do not supply
the tools. On a cross-built target run explicitly:

```sh
python3 test/test_demux_mkv.py build/test/test_demux_mkv
build/test/test_demux_mkv recording.mkv packets.csv
```

The MP4 fixture comparison checks unchanged packet payloads, EOF and seek.
It does not exercise repeated close/reopen for MP4. Existing MP4 behavior is
outside the scope of this backend.

Reference: [FFmpeg demuxing API](https://ffmpeg.org/doxygen/trunk/group__lavf__decoding.html)
and [bitstream filter API](https://ffmpeg.org/doxygen/trunk/group__lavc__bsf.html).
