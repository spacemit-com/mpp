```
./configure --prefix=/usr/local --enable-gpl --enable-nonfree --enable-filter=delogo --enable-debug --disable-optimizations --enable-libspeex --enable-shared --enable-pthreads --enable-libx264

make

sudo make install
```

```
fuqiang@ubuntu:/usr/local/ffmpeg/lib$ tree
.
├── libavcodec.a
├── libavcodec.so -> libavcodec.so.59.37.100
├── libavcodec.so.59 -> libavcodec.so.59.37.100
├── libavcodec.so.59.37.100
├── libavdevice.a
├── libavdevice.so -> libavdevice.so.59.7.100
├── libavdevice.so.59 -> libavdevice.so.59.7.100
├── libavdevice.so.59.7.100
├── libavfilter.a
├── libavfilter.so -> libavfilter.so.8.44.100
├── libavfilter.so.8 -> libavfilter.so.8.44.100
├── libavfilter.so.8.44.100
├── libavformat.a
├── libavformat.so -> libavformat.so.59.27.100
├── libavformat.so.59 -> libavformat.so.59.27.100
├── libavformat.so.59.27.100
├── libavutil.a
├── libavutil.so -> libavutil.so.57.28.100
├── libavutil.so.57 -> libavutil.so.57.28.100
├── libavutil.so.57.28.100
├── libpostproc.a
├── libpostproc.so -> libpostproc.so.56.6.100
├── libpostproc.so.56 -> libpostproc.so.56.6.100
├── libpostproc.so.56.6.100
├── libswresample.a
├── libswresample.so -> libswresample.so.4.7.100
├── libswresample.so.4 -> libswresample.so.4.7.100
├── libswresample.so.4.7.100
├── libswscale.a
├── libswscale.so -> libswscale.so.6.7.100
├── libswscale.so.6 -> libswscale.so.6.7.100
├── libswscale.so.6.7.100
└── pkgconfig
    ├── libavcodec.pc
    ├── libavdevice.pc
    ├── libavfilter.pc
    ├── libavformat.pc
    ├── libavutil.pc
    ├── libpostproc.pc
    ├── libswresample.pc
    └── libswscale.pc

1 directory, 40 files
```

```
fuqiang@ubuntu:/usr/local/ffmpeg/include$ tree
.
├── libavcodec
│   ├── ac3_parser.h
│   ├── adts_parser.h
│   ├── avcodec.h
│   ├── avdct.h
│   ├── avfft.h
│   ├── bsf.h
│   ├── codec_desc.h
│   ├── codec.h
│   ├── codec_id.h
│   ├── codec_par.h
│   ├── d3d11va.h
│   ├── defs.h
│   ├── dirac.h
│   ├── dv_profile.h
│   ├── dxva2.h
│   ├── jni.h
│   ├── mediacodec.h
│   ├── packet.h
│   ├── qsv.h
│   ├── vdpau.h
│   ├── version.h
│   ├── version_major.h
│   ├── videotoolbox.h
│   ├── vorbis_parser.h
│   └── xvmc.h
├── libavdevice
│   ├── avdevice.h
│   ├── version.h
│   └── version_major.h
├── libavfilter
│   ├── avfilter.h
│   ├── buffersink.h
│   ├── buffersrc.h
│   ├── version.h
│   └── version_major.h
├── libavformat
│   ├── avformat.h
│   ├── avio.h
│   ├── version.h
│   └── version_major.h
├── libavutil
│   ├── adler32.h
│   ├── aes_ctr.h
│   ├── aes.h
│   ├── attributes.h
│   ├── audio_fifo.h
│   ├── avassert.h
│   ├── avconfig.h
│   ├── avstring.h
│   ├── avutil.h
│   ├── base64.h
│   ├── blowfish.h
│   ├── bprint.h
│   ├── bswap.h
│   ├── buffer.h
│   ├── camellia.h
│   ├── cast5.h
│   ├── channel_layout.h
│   ├── common.h
│   ├── cpu.h
│   ├── crc.h
│   ├── csp.h
│   ├── des.h
│   ├── detection_bbox.h
│   ├── dict.h
│   ├── display.h
│   ├── dovi_meta.h
│   ├── downmix_info.h
│   ├── encryption_info.h
│   ├── error.h
│   ├── eval.h
│   ├── ffversion.h
│   ├── fifo.h
│   ├── file.h
│   ├── film_grain_params.h
│   ├── frame.h
│   ├── hash.h
│   ├── hdr_dynamic_metadata.h
│   ├── hdr_dynamic_vivid_metadata.h
│   ├── hmac.h
│   ├── hwcontext_cuda.h
│   ├── hwcontext_d3d11va.h
│   ├── hwcontext_drm.h
│   ├── hwcontext_dxva2.h
│   ├── hwcontext.h
│   ├── hwcontext_mediacodec.h
│   ├── hwcontext_opencl.h
│   ├── hwcontext_qsv.h
│   ├── hwcontext_vaapi.h
│   ├── hwcontext_vdpau.h
│   ├── hwcontext_videotoolbox.h
│   ├── hwcontext_vulkan.h
│   ├── imgutils.h
│   ├── intfloat.h
│   ├── intreadwrite.h
│   ├── lfg.h
│   ├── log.h
│   ├── lzo.h
│   ├── macros.h
│   ├── mastering_display_metadata.h
│   ├── mathematics.h
│   ├── md5.h
│   ├── mem.h
│   ├── motion_vector.h
│   ├── murmur3.h
│   ├── opt.h
│   ├── parseutils.h
│   ├── pixdesc.h
│   ├── pixelutils.h
│   ├── pixfmt.h
│   ├── random_seed.h
│   ├── rational.h
│   ├── rc4.h
│   ├── replaygain.h
│   ├── ripemd.h
│   ├── samplefmt.h
│   ├── sha512.h
│   ├── sha.h
│   ├── spherical.h
│   ├── stereo3d.h
│   ├── tea.h
│   ├── threadmessage.h
│   ├── timecode.h
│   ├── time.h
│   ├── timestamp.h
│   ├── tree.h
│   ├── twofish.h
│   ├── tx.h
│   ├── uuid.h
│   ├── version.h
│   ├── video_enc_params.h
│   └── xtea.h
├── libpostproc
│   ├── postprocess.h
│   ├── version.h
│   └── version_major.h
├── libswresample
│   ├── swresample.h
│   ├── version.h
│   └── version_major.h
└── libswscale
    ├── swscale.h
    ├── version.h
    └── version_major.h

8 directories, 137 files
```