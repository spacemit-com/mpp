# Fixed DMA-BUF input: review follow-up and live-camera validation

This supplements `vdec_dmabuf_pool_validation.md`. The reviewed PR revision was
`e2a7ac55c7d64d04228866f98c7dfc629f3bba9d` (PR #51). The review comment was last
updated at `2026-09-16T09:14:36Z`.

## Initialization failure ownership

`al_dec_create()` owns the mutexes and condition variable; the caller must pair
every successfully created context with `al_dec_destory()`, including after a
failed `al_dec_init()`. MPI does this through `VDEC_DestroyChn()` and
`vdec_plugin_close()`. Destroying the locks in the `eventfd()` failure branch
would invalidate the later destructor, which locks/broadcasts/joins before
destroying synchronization. That proposed review fix is therefore not applied.

There was a separate real failure-path leak: the destructor closed the device
only when both `nVideoFd` and `stCodec` were nonzero. A successful device open
followed by `fcntl()` or `createCodec()` failure leaked the fd, and fd zero was
incorrectly treated as invalid. Initialize the fd to -1 and close any owned
nonnegative fd independently of codec construction. Log `eventfd()` failure and
document the existing caller-owned cleanup contract.

`test_vdec_init_cleanup` compiles the actual decoder implementation with
test-local device/allocator substitutions; production has no injected hooks.
It runs 25 iterations each of eventfd failure, device-open failure, codec-create
failure, and codec-create failure with device fd zero. It checks stable fd count
and exactly two successful mutex destructions plus one condition destruction.
Reinstating the old fd-close condition makes the test fail; the fix passes.
This is a deterministic cleanup test, not a real-driver failure simulation.

## Slot capacity versus temporary exhaustion

The original `size > capacity` condition already accepted an exact-fit packet;
the review's proposed comparison was identical to the existing comparison.
Keep that comparison, but return `SYS_ERR_INVAL` for an oversized payload.
`SYS_ERR_FULL` remains the transient queue/slot exhaustion result. Neither case
resizes the pool or allocates a replacement buffer in the frame path.

The hardware pool regression now tests `capacity-1`, `capacity`, and
`capacity+1`, including successful reuse after an oversized packet is rejected.
The existing all-slots-leased test still expects `SYS_ERR_FULL`.

## Date and CI infrastructure findings

The original document date, 2026-09-16, matches the review's own timestamp and
the Git commit date. It is not changed to an invented earlier date. The CI
worker-cache non-fast-forward fetch and unreachable worker described in the bot
comment are infrastructure errors; this patch does not modify worker caches or
CI servers. The bot reported falling back to its baseline review.

## UVC base-reference ownership

Live tests found `VB_ModReleaseBuffer` rejecting an extra UVC release during
shutdown on both legacy and fixed-pool input. The capture thread can have
returned its base reference before the recycle thread stops; teardown must not
release that reference again, even if downstream references remain.

Track only the capture/driver base reference per slot. Record it after the
initial `VB_ModGetBuffer` and a successful recycle QBUF, clear it on capture
release, and release only still-owned base references at teardown. Capture and
recycle changes use the UVC context mutex; teardown joins both workers before
freeing the buffers. Consumer/depth-queue ownership is unchanged.

## Validation environment and reproducible tests

- Host: x86 Debug build, cleanup/input-retry/zero-DRI tests. The hardware DMA pool
  test skips with code 77 when `/dev/dma_heap/linux,cma` is absent.
- Hardware: K3 10.0.91.119, Linux 6.18.3-generic, native RelWithDebInfo build.
- Independent staging: `/root/uav-dmabuf-validation-20260916`; CPU 0-7 only.
- Use staged `LD_LIBRARY_PATH` and `MPP_PLUGIN_DIR`; no system-library, kernel,
  DTB or frequency changes. Stop competing camera applications before testing.

```sh
cmake -S . -B build-review -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_TESTS=ON -DBUILD_ROS2_EXAMPLES=OFF
cmake --build build-review --parallel 8
export LD_LIBRARY_PATH="$PWD/build-review/lib"
export MPP_PLUGIN_DIR="$PWD/build-review/al/vcodec"
ctest --test-dir build-review --timeout 20 --output-on-failure \
  -R '^(test_sys|test_vb|test_integration|test_multiproc|test_mux_common|test_mux_socket_disconnect|test_vdec_input_retry|test_vdec_init_cleanup|test_mjpeg_zero_dri)$'
build-review/test/test_sys_stream_dmabuf_pool
for mode in empty queued eos; do
  build-review/test/test_vdec_bound_dmabuf_input test/assets/1920x1080.jpg "$mode"
done
```

The normal full CTest set is intentionally not used here: `test_uvc` modifies
camera controls, and `test_uvc_vdec` is an interactive/manual streaming sample.

Earlier live integration on the same board used USB MJPEG 4000x1200 at 60 FPS,
640x400 stereo outputs, raw/fused embedded IMU, and original MJPEG recording.
The bridge explicitly opted into twelve 6 MiB input slots. The extra UVC fix in
this follow-up was present in those successful tests:

- Stereo 60.096 FPS, raw/fused IMU 600.962 Hz. No DMA heap allocations observed
  in the publishing process after the startup interval (5 seconds).
- 20 SDK start/capture/stop cycles: 1200 frames and 12020 valid IMU samples,
  no backward timestamps or duplicate-release errors; CMA used returned to the
  1346-page baseline and kernel taint stayed zero.
- Four 6 MiB slots also sustained normal 60.096 FPS live capture and recording.
- MP4 packet-copy remux/replay reached EOS with the same stereo/IMU rates.
- A separate buffered-write/CMA pressure test retained zero hot allocations but
  had recording gaps and expired consumer descriptors. It is **not** a full
  no-drop integration PASS and does not prove the kernel allocator fixed.

The integration harness/fixture belong to the external camera application, not
this MPP repository. These measurements do not claim full VIO/LAS2/nvblox/DRM,
multi-camera, or day-long reliability validation.

### Retest with all review fixes applied

- Host cleanup/input-retry/zero-DRI CTest: 3/3 PASS. K3 selected CTest: 9/9 PASS.
- Fixed pool: capacity boundaries, exhaustion/reuse and 200 receive/unbind
  races PASS. Bound VDEC empty/queued/EOS hardware modes all PASS.
- Five further SDK start/capture/stop cycles: 300 frames, 3005 valid IMU samples;
  no duplicate-release errors or backward timestamps.
- A new 60-second live capture/record/receive run measured stereo 59.841 FPS,
  raw IMU 598.448 Hz and fused IMU 598.451 Hz. Publisher startup allocations:
  64; allocations after 5 seconds: 0; OUTPUT QBUF: 3458 DMABUF, 0 MMAP.
- Recording contained 3430 frames over a 57.35808-second source span, with
  four missing published sequence indices and a maximum exposure-time gap of
  249.6 ms. IMU's maximum gap was 232.96 ms. This longer run is not presented
  as lossless, even though it completed without publisher/receiver error logs.
  Packet/index sizes/counts matched and timestamps remained monotonic.
- CMA actual usage returned to 1346 pages and kernel taint remained zero.
- Decoder pixel A/B (`4000 1200 MODE 64 12 0 1 1`): both modes hashed
  32 warmup plus 64 measured NV12 frames to `15d8f2152717f030`; zero PTS
  errors. Measured input allocations were 64 for legacy and zero for DMA-BUF.

The expected acceptance remains fewer hot allocations/copies, not higher FPS
or guaranteed lossless live recording.
