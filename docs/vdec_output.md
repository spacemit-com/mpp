# VDEC output ownership

Set `VdecChnAttr.eOutputMode` before creating the channel:

- `VDEC_OUTPUT_PULL` (zero/default): `VDEC_GetFrame` drives recycling and
  readout. There is no output thread or MPI depth queue. Applications must
  interleave sending and receiving, or use separate input and readout threads;
  sending an entire stream before reading can exhaust decoder buffers.
- `VDEC_OUTPUT_BIND`: one output worker drives the same readout core and sends
  frames through `SYS_SendFrame`. `GetFrame` and `GetLatestFrame` return
  `ERR_VDEC_NOT_SUPPORT`. Missing/full sinks drop frames without a pull fallback.

Callers explicitly select the mode rather than relying on zero initialization:

| Caller output path | Mode |
| --- | --- |
| Application calls `GetFrame` / `GetLatestFrame`, including bound DEMUX/UVC input | `VDEC_OUTPUT_PULL` |
| VDEC sends decoded frames through a SYS output binding, such as VDEC -> VENC | `VDEC_OUTPUT_BIND` |

`sample_file_transcode` and the bind paths in `sample_uvc_vdec_venc` and
`sample_uvc_vdec_venc_rtsp` use BIND. Other sample/test creation sites that read
frames in the application explicitly use PULL. In particular, the bind path of
`sample_uvc_vdec` binds only the input and still uses PULL output.

Output bindings and application readout are mutually exclusive. To change mode,
disable and recreate the channel with the new attributes. Input stream bindings
(`DEMUX/UVC -> VDEC`) work in either output mode. Establish output bindings before
sending input if initial frames must not be dropped.

The current Linlon VENC implementation always uses external DMA-BUF input and
ignores `VencChnAttr.eFrameBufMode`. In `VDEC -> VENC` pipelines, the input frames
belong to the VDEC VB pool; no buffer-mode setting is required.

`GetLatestFrame` waits for the first frame, then probes a bounded batch of ready
frames without waiting for more input. It retains the latest candidate across
pool replacement and releases older candidates. It does not drain in background.
Finite `GetFrame` waits share a deadline across recycling and readout; zero is
nonblocking and `(U32)-1` waits until a frame, EOS, or an error is observed.

The internal pool tracks three producer states:

| State | VDEC base reference | Queued to CAPTURE |
| --- | --- | --- |
| OWNED | yes | no |
| QUEUED | yes | yes |
| RELEASED | no | no |

RELEASED does not imply globally free. VB acquires the buffer only after all
consumer references are gone. QBUF failures retain OWNED references and latch an
output error. An explicit Flush/Reset can recover; Disable always stops hardware
before releasing tracked references, including partial initialization failures.

`outputLock` serializes CAPTURE operations, base references, and pool replacement.
Control/status operations acquire `outputLock` before the channel `lock`.
Input submission serializes packets with `inputLock`. A submission attempt holds
the channel `lock` only while checking state and calling AL; queue-full retries
release that lock and sleep 2 ms. Flush/Reset can acquire it between attempts.
Flush/Reset pauses submission under `lock`, stops CAPTURE, and requeues producer
buffers. It does not overwrite consumer-held frames. Locks live from Init to Exit.
Applications must still stop/join SendStream and GetFrame callers before Disable.

AL reports the actual REQBUFS count and per-plane allocation requirements through
`al_dec_get_output_requirement`. Every resolution change replaces the MPI pool,
even if dimensions shrink. Retired pools retain only IDs and are reclaimed by
calling `VB_DestroyPool`; the VB API retains its synchronous BUSY contract.
Outstanding consumer frames survive Disable. Destroy/re-enable waits for those
references to be released. No asynchronous VB destruction is introduced.

Rebuild applications and the codec plugin with these headers: `VdecChnAttr` has
an added mode field, the decoder plugin must export the new requirement query,
while the input submission interface remains unchanged.

## Validation

The hardware-independent tests cover output ownership, PULL/BIND exclusion,
bounded Latest readout, deadlines, EOS, resolution changes, held frames during
shutdown, QBUF failure recovery, actual REQBUFS counts, and plane padding.
`test_vdec_buffer_payload` also reproduces a real empty CAPTURE marker whose
`bytesused` equals `data_offset`; it must not count as a decoded frame.

```sh
cmake --build build-vdec-pull --target test_vdec test_vdec_input_retry test_vdec_output test_vdec_output_requirement test_vdec_buffer_payload
ctest --test-dir build-vdec-pull -R '^test_vdec($|_input_retry$|_output$|_output_requirement$|_buffer_payload$)' --output-on-failure
```

Validated on K3 / Linlon hardware with a privately mounted test plugin:

- H.264, H.265, MJPEG PULL readout.
- MJPEG backpressure: 108 submitted/decoded frames in each of direct-input and
  bound-input modes, with zero PTS errors.
- H.264 640x360 -> 1280x720 -> 640x360: 36 decoded frames; four held frames
  remained unchanged across pool replacement and Disable, then released safely.
- DEMUX -> VDEC -> VENC: 20 encoded 1920x1080 frames and clean process exit.
- Output ownership tests passed AddressSanitizer, UndefinedBehaviorSanitizer,
  and leak detection.

The UVC examples were rebuilt; camera/RTSP end-to-end runs were not performed.
