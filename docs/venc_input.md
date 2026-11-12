# VENC input memory

`VencChnAttr.eFrameBufMode` selects the memory passed to the Linlon encoder:

| Mode | Input |
| --- | --- |
| `VENC_FRAME_BUF_DMABUF_EXTERNAL` | External DMA-BUF in `stVFrame.u32Fd[0]` |
| `VENC_FRAME_BUF_USERPTR_EXTERNAL` | External virtual address for each plane in `stVFrame.ulPlaneVirAddr[]` |
| `VENC_FRAME_BUF_DMABUF_INTERNAL` / `VENC_FRAME_BUF_NORMAL_INTERNAL` | Legacy aliases for external DMA-BUF, preserving zero-initialized attributes |

USERPTR does not allocate or copy input pixels and does not require DMA-BUF fds.
Set `stVFrame.u32PlaneNum`, each plane address and `u32PlaneSize[]` to describe
caller-owned storage. Planes may be separately allocated. Their layout must
match the channel format and the driver's negotiated stride and buffer size,
including padding. A nonzero `u32PlaneStride[]` is checked against that stride;
zero asserts that the caller already follows the negotiated layout.
NV12/NV21 use two planes, I420/YV12 use three planes in their format order, and
packed RGBA/ARGB/BGRA/ABGR/YUYV/UYVY use one. Actual format availability depends
on the codec driver; the K3 driver used for validation does not advertise YV12
(`V4L2_PIX_FMT_YVU420M`). Invalid layouts are rejected before queueing a buffer. Keep all other channel attributes configured as for DMA-BUF.

For example, a 640 x 480 NV12 frame with 16-byte stride alignment uses a 640-byte
Y and UV stride on the K3 encoder, with 307200 and 153600 bytes respectively:

```c
attr.eInputPixelFormat = MPP_PIXEL_FORMAT_NV12;
attr.u32Width = 640;
attr.u32Height = 480;
attr.u32Align = 16;
attr.eFrameBufMode = VENC_FRAME_BUF_USERPTR_EXTERNAL;

VideoFrameInfo frame = {0};
frame.stVFrame.u32PlaneNum = 2;
frame.stVFrame.ulPlaneVirAddr[0] = (UL)y_pixels;
frame.stVFrame.ulPlaneVirAddr[1] = (UL)uv_pixels;
frame.stVFrame.u32PlaneStride[0] = 640;
frame.stVFrame.u32PlaneStride[1] = 640;
frame.stVFrame.u32PlaneSize[0] = 640 * 480;
frame.stVFrame.u32PlaneSize[1] = 640 * 480 / 2;
frame.stVFrame.u64PTS = pts_us;
VENC_SendFrame(chn, &frame, 0);
```

`VENC_SendFrame` is asynchronous. Successful submission does not indicate that
the input has been consumed. For VB-backed frames, a nonzero `ulBufferId` lets
VENC retain a VB reference until the input queue reports completion. The caller
can release its own reference after successful submission, but cannot overwrite
the pixels while the encoder owns them.

For external input with `ulBufferId == 0`, retain all pixel storage and keep it
unchanged until the channel has been successfully disabled and destroyed. The
current public API does not report per-frame external input completion. Receiving
an encoded packet does not establish that its input memory is no longer accessed.
The encoder does not free caller-owned storage.

An EOS-only frame (`MPP_FRAME_FLAG_EOS`, `u32PlaneNum == 0`) does not need pixel
storage. A frame carrying both pixels and EOS must pass the normal plane checks;
its EOS state is committed only after successful submission. Rejected input is
not accepted by the encoder, so the caller keeps ownership and can correct or
release it. `VENC_SendFrame` currently returns immediately when no input slot is
available; its timeout parameter does not wait for a slot.

The internal `AlEncInputBufferDoneCb` releases VENC's reference for VB-backed
input after driver input completion. It is not an application completion API and
does not return external inputs without VB handles.

## Validation

Configure with `BUILD_TESTS=ON`, then build and run the hardware-independent
USERPTR regression:

```sh
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build --target test_venc_userptr
ctest --test-dir build --output-on-failure -R '^test_venc_userptr$'
```

The test exercises the actual AL implementation with fake driver operations:
per-plane addresses, negotiated stride and capacity checks, invalid layouts,
buffer-slot reuse, submission failures and EOS state. It does not require a
codec device.

On the target device, build `test_venc_userptr_hw` and run with matching MPP and
Linlon plugin builds, for example:

```sh
LD_LIBRARY_PATH="$PWD/build/lib" timeout 45s \
    ./build/test/test_venc_userptr_hw h264 /tmp/userptr.h264 nv12
ffmpeg -v error -i /tmp/userptr.h264 -f null -
```

The hardware test submits 24 frames using anonymous per-plane mappings without
input VB handles or DMA-BUF fds, drains encoded output and EOS, then destroys the
channel before unmapping input storage. It accepts `h264`, `h265` or `mjpeg`, and
optional input formats `nv12`, `i420`, `yv12` or `rgba`; availability depends on the
driver. The saved stream can be decoded independently to verify its frame count.
