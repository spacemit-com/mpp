# Fixed compressed-input DMA-BUF pool: K3 validation

Date: 2026-09-16. Acceptance criterion: fewer compressed-payload copies and no
per-frame CMA allocation, not an FPS improvement.

For the subsequent review responses, failure-injection regression, and separate
live-camera results, see [the review follow-up](vdec_dmabuf_pool_review.md).

**Historical blocker from initial testing on 10.0.90.98:** the fixed input pool
completed 5,400 frames under buffered-write/CMA pressure without hot allocations,
but startup triggered a kernel `list_del corruption` warning in
`remove_wait_queue -> poll_freewait -> do_sys_poll`. Do not interpret the tests
below alone as full release approval. The subsequent lifecycle repair and
validation are recorded separately below. Detailed initial evidence is retained in
`/home/yan/k3_ws_copy/.artifacts/cma-98-20260916/RESULTS.md`.

## Scope and environment

- K3 `10.0.91.119`, independent staging directory `/root/mpp-dmabuf-pool`.
- Worktree `feature/vdec-dmabuf-input-pool`, based on `57af28f`, plus local
  sustained-use fixes described below. These fixes were not pushed during this test.
- Native Release build (`build-bench`). Libraries and codec plugin loaded from
  the staging directory with `LD_LIBRARY_PATH` and `MPP_PLUGIN_DIR`.
- Fixture: 240 original MJPEG packets, copied without re-encoding from 10 seconds
  into `/root/output.mkv`; 4000 x 1200, mean 378,535 bytes, maximum 444,451 bytes.
  Repeated from memory so disk/demux/USB do not affect the measured interval.
- No USB camera was enumerated during this validation. This is a SYS-to-hardware-
  VDEC test, not a live-camera or complete UAV pipeline benchmark.
- Both modes use the same executable, 32 warmup frames, eight output buffers,
  and at most four outstanding frames. Direct mode uses twelve 6 MiB input slots.

## Results

Three alternating-order, unpaced runs, 600 measured frames per run:

| Metric | Legacy copy input | Fixed DMA-BUF input |
|---|---:|---:|
| FPS, three runs | 104.670 / 105.403 / 103.955 | 106.371 / 104.741 / 106.312 |
| Process CPU ms/frame | 1.1373 / 1.1408 / 1.1377 | 1.4656 / 1.4808 / 1.4985 |
| Input allocations during each 600-frame interval | 600 | 0 |
| Cross-buffer compressed-payload copies, source inspection | 3/frame | 1/frame |
| PTS ordering errors | 0 | 0 |

No material throughput gain is established; direct mode used more process CPU
in this benchmark. The useful change is allocation/copy reduction. Copies above
exclude any in-place JPEG DRI=0 marker removal and kernel-internal work.

Sustained direct-mode run, paced at 60 FPS:

- 3,600 measured frames over approximately 60 seconds: **59.988 FPS**.
- Input allocations in measured interval: **0**. Startup allocations: 12 input
  slots plus eight output buffers; no hot-path resize.
- PTS ordering errors: **0**; every submitted frame was received and released.
- Send-to-decoded latency: p50 27.218 ms, p95 28.853 ms. This excludes camera
  capture/USB and is not camera-to-display latency.
- Process CPU: 8.887% of one core; 1.4814 ms/frame.
- CmaFree before/after the test: 160,140 / 173,940 KiB. This global counter is
  noisy; the primary allocation evidence is an interposed `dma_alloc_buf()`
  counter, not an assertion that all system/driver CMA use is constant.

Correctness and lifetime checks:

- Both paths hashed every visible NV12 byte of the same 64 decoded frames
  (32 warmup + 32 measured): identical FNV-1a checksum `3c5726c46851e96a`.
  Pixel hashing is excluded from performance runs.
- One-slot and two-slot direct pools: each completed 120 measured frames with
  zero PTS errors and zero hot-path allocations. Input reuse did not stall.
- Fixed pool exhaustion, lease-protected unbind, fd reuse: PASS.
- MJPEG zero-DRI normalization and decoder retry unit regressions: PASS.

## Sustained-use fixes needed on top of the pushed branch

The original one-frame smoke test did not expose these issues. Do not merge the
earlier pushed tip without its follow-up fixes.

1. Returning input leases only when the next packet arrives deadlocks when every
   ring slot is leased. Reproduced at 12 submitted/decoded frames. The existing
   codec event thread now reaps input DQBUF independently, with eventfd wakeup
   and nonblocking V4L2 DQBUF. No extra polling/sleep thread was introduced.
2. Legacy input-buffer handling returned the extra-id sentinel rather than the
   successful QBUF status. Correct the return value.
3. Select direct input from the configured V4L2 memory type, not a zero-initialized
   stream fd. Legacy callers may leave that newly added field at zero.
4. Return unsubmitted leases on stop/failure, and transfer ownership only after
   successful QBUF to avoid duplicate release on failure.
5. Pair CPU DMA-BUF START/END synchronization for fixed-slot accesses, including
   the copying compatibility receive API.
6. Quiesce the blocking input/event poll before capture queue reconstruction or
   flush. Wake with eventfd, then wait for acknowledgement after `poll()` and
   event processing have completed; merely waking the thread is not sufficient.
   Join the event thread before teardown. Protect source-change notification
   with an atomic exchange so a newly arriving event is not overwritten.
7. Keep unbind lease validation and queue reset under one queue lock. A receiver
   that has already looked up the binding must not create a lease in an
   unlock/relock gap. The reset helper used here does not lock recursively.
8. Assert the 16-bit token fields and signed 32-bit plugin limit at compile
   time, and reject unencoded high bits on release.

## Lifecycle repair on 10.0.90.98

The old diagnostic log places capture `STREAMOFF` / `REQBUFS` reconstruction
in one thread while another thread may already be blocked in device `poll()`.
The kernel warning is in that poll syscall's wait-list cleanup. This establishes
a concrete lifetime overlap to avoid; it does not identify the exact vendor
driver instruction which damages the list or imply that this concurrency is
prohibited by the generic V4L2 API.

The new direct path uses a mutex/condition-variable pause handshake, separate
from the input-buffer mutex. Lifecycle code must not hold the input mutex while
waiting for acknowledgement. Normal frame processing has no added sleep, and
no additional worker thread is created. Legacy MMAP operation remains available.

Testing uses a freshly rebooted, initially untainted K3 and an isolated native
RelWithDebInfo build under `/root/cma-pool-validation-20260916/source/build-fix`.
Neither production UAV libraries nor the kernel/DTB were replaced.

Completed lifecycle regressions:

- 100 full direct-mode startup / initial source-change / decode / teardown runs,
  33 frames per run: PASS. This is initial source-change coverage, not a claim
  to cover every mid-stream resolution transition.
- 20 stops before any input, 20 stops with four queued packets, and 20 single-
  frame decode/stop runs using `test_vdec_bound_dmabuf_input`: PASS.
- One-slot and two-slot reuse (120 measured frames each), fixed-ring exhaustion
  and lease tests, retry and zero-DRI tests: PASS.
- Legacy/direct pixel hashes of 64 frames remain `3c5726c46851e96a`.
- Kernel taint remained zero. CMA bitmap `used` returned to its 1,958-page
  baseline after both restart and early-stop batches.

Buffered-write pressure after the repair:

- 5,400 measured 4000 x 1200 MJPEG frames over 180 seconds: 30.001 FPS,
  zero hot-path allocations and zero PTS errors.
- Twelve input slots, eight output buffers, and 231 MiB of additional fixed
  CMA allocations: 93,590 CMA pages held at the start of measurement.
- 32 MiB/s buffered writes in 256 KiB chunks: 6,053,691,392 bytes written.
- Decoder latency p50 27.997 ms / p95 29.562 ms; process CPU 1.4468 ms/frame.
- Normal exit, kernel taint zero. Logs:
  `runs/dmabuf-fixed-1789546828176347409/` under the validation directory.
- This fresh 32 GiB board had much more free memory than the previous run.
  Sampled `CmaFree` remained 149,928 KiB, versus a 96 KiB minimum in the earlier
  run. Fixed CMA occupancy and write rate match, but fragmentation/migration
  pressure do not. Do not describe this as reproducing or fixing the original
  kernel ENOMEM while the decoder runs.
- A separate 60 FPS run completed 3,600 measured frames at 59.988 FPS, with
  zero hot allocations / PTS errors; latency p50 27.261 ms / p95 28.856 ms.
  Kernel taint stayed zero and CMA bitmap usage returned to 1,958 pages.

These tests do not repair the independent dirty-file-folio CMA migration
failure. Preallocation removes that repeated allocation trigger during steady
state; startup, pool growth, resolution changes and other components can still
require new CMA allocations. A bounded regression cannot prove absence of all
driver races. The production camera/UAV path remains unchanged and opt-in.

## PR #51 review follow-up

The reviewed tip was `57af28f`. The unbind unlock/relock window was real even
though the review's description of recursive double locking was not. It is
closed by the queue-locked reset helper. A concurrent receive/unbind test holds
any returned lease until unbind has completed: both operations must never report
success. All 200 iterations passed on K3.

The configured token limits are 128 binds and 16 slots, so the original code
did not overflow those fields. Static assertions now prevent incompatible
future limits, including the signed `Buffer::nExtraId` constraint. A high-bit
malformed-token regression also passed.

The proposed immediate EOS release was deliberately not applied: a successful
QBUF transfers ownership to hardware. The independent reaper releases on input
DQBUF, or stream-off releases the remaining lease during teardown. MPI returns
only unaccepted inputs. The `eos` test sends a JPEG followed by a zero-byte EOS,
waits for `ERR_VDEC_EOS`, then checks channel teardown and unbind. Twenty runs
passed, along with another 20 empty and 20 queued-stop runs. Retry/zero-DRI
tests and the legacy/direct pixel-hash comparison passed again after these
review changes. Native K3 and x86 Debug builds passed; the x86 CMA test skipped
with status 77 because that host has no DMA heap.

After the review changes, the 180-second pressure test was repeated:
5,400 frames at 30.001 FPS, 6,055,264,256 bytes written, zero hot allocations
and zero PTS errors; p50 28.209 ms / p95 29.782 ms, CPU 1.4836 ms/frame.
The process exited normally and kernel taint remained zero. Evidence:
`runs/pr-final-1789548725984418611/` in the same validation directory.

## Reproduction

From the staging repository, after building the relevant targets:

```sh
export LD_LIBRARY_PATH="$PWD/build-bench/lib"
export MPP_PLUGIN_DIR="$PWD/build-bench/al/vcodec"

./build-bench/test/test_vdec_input_benchmark \
  bench-data/output-240.mjpg 4000 1200 legacy 600 12 0
./build-bench/test/test_vdec_input_benchmark \
  bench-data/output-240.mjpg 4000 1200 dmabuf 600 12 0
./build-bench/test/test_vdec_input_benchmark \
  bench-data/output-240.mjpg 4000 1200 dmabuf 3600 12 60

# Optional visible-pixel hash; do not use its timing as decode performance.
./build-bench/test/test_vdec_input_benchmark \
  bench-data/output-240.mjpg 4000 1200 legacy 32 12 0 1
./build-bench/test/test_vdec_input_benchmark \
  bench-data/output-240.mjpg 4000 1200 dmabuf 32 12 0 1

# Bounded lifecycle regressions (run on an otherwise idle hardware decoder).
timeout 15 ./build-bench/test/test_vdec_bound_dmabuf_input \
  test/assets/1920x1080.jpg empty
timeout 15 ./build-bench/test/test_vdec_bound_dmabuf_input \
  test/assets/1920x1080.jpg queued
timeout 15 ./build-bench/test/test_vdec_bound_dmabuf_input \
  test/assets/1920x1080.jpg decode
timeout 15 ./build-bench/test/test_vdec_bound_dmabuf_input \
  test/assets/1920x1080.jpg eos
```

Logs remain in `/root/mpp-dmabuf-pool/bench-logs`. The new path remains opt-in
and same-process only. These tests do not change the production camera/UAV
configuration, establish long-term fragmentation behavior, or prove live USB
reconnect, multi-camera or resolution-change handling.
