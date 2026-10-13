cmake -S . -B build -DBUILD_ROS2_EXAMPLES=AUTO/ON/OFF
cmake --build build --target build_ros2_examples

* `publish_mode:="copy"/"mpp_zero_copy"`
* `./test_rtsp_pull rtsp://<ip>:<port>/<path>`: verify RTSP pull with DEMUX
* `bash scripts/run_rtsp_pull_on_board.sh --host <board_ip> --rtsp-url rtsp://<ip>:<port>/<path>`: build, upload, and run board-side verification

## Packed RGB encoder regression (no hardware)

```bash
cmake -S . -B build -DBUILD_TESTS=ON -DBUILD_ROS2_EXAMPLES=OFF
cmake --build build --target test_venc_rgb_mapping test_venc_rgb_layout --parallel 2
ctest --test-dir build -R '^test_venc_rgb_' --output-on-failure
```

These tests use the actual plugin mapping and format-request implementation,
with unused hardware entry points discarded and `ioctl` scripted in the layout
test. They do not open a camera, VPU, DMA heap or network connection. They cover
all four packed RGB byte orders, aligned/padded row sizes, stale planar state,
driver-returned padding and unchanged NV12/YUYV layout. They also check the
BGRA/ABGR mapping against the Linux byte-order table, not just a self-consistent
round trip. Both tests fail before the fix.

Hardware validation is separate: encode known BGRA color bars and asymmetric
top/bottom stripes through DMA-BUF input, then decode and check channel order
and row alignment. Do not treat the software tests as a VPU encode test.
