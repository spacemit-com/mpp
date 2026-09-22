# test/assets

测试素材不随仓库分发，统一托管在 Nexus（[video:mpp](https://nexus.bianbu.xyz/#browse/browse:video:mpp)）。

克隆仓库后在仓库根目录执行：

```bash
bash test/download_assets.sh
```

脚本会下载全部素材到本目录并做 sha1 校验；已存在且校验通过的文件自动跳过，`--force` 可强制重新下载。

| 文件 | 用途 |
| --- | --- |
| `input.264` | H.264 裸流，1080p |
| `input.265` | H.265 裸流，1080p |
| `input.mjpeg` | MJPEG 裸流 |
| `test_video.mp4` | MP4 封装测试源 |
| `test_video.ts` | TS 封装测试源 |
| `1920x1080.jpg` | JPEG 解码测试图 |
| `vi_phy0_last_frame.yuv` | VI 末帧 YUV 参考，用于回归比对 |
