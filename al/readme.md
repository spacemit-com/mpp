# AL plugins

Codec and VI backends are shared libraries (`libv4l2_linlonv5v7_codec.so`,
`libvi_k3_cam_plugin.so`, ...) loaded at runtime by `al/module.c`.

## Lookup order

The first existing `lib<name>.so` wins:

1. `$MPP_PLUGIN_DIR`: colon-separated directories
2. `/usr/lib`, `/usr/local/lib`
3. `/system/lib64`, `/vendor/lib64` (`lib<name>.z.so`, OpenHarmony)

## Running a build tree

Building never installs plugins. They are written to `<build>/lib` next to
`libspacemit_mpp.so`, so point the loader there to run what was just built:

```sh
MPP_PLUGIN_DIR="$PWD/build/lib" ./build/test/test_venc_userptr_hw h264 /tmp/out.h264 nv12
```

CTest sets `MPP_PLUGIN_DIR` for every registered test. Without it the loader
falls back to the installed plugins, which may not match the build.

## Installing

Install explicitly, or use the Debian package. Plugins go to `<prefix>/lib`:

```sh
cmake --install build --prefix /usr/local
```
