# AI / AO 音频采集与播放

AI（Audio Input）从声卡采集 PCM，AO（Audio Output）向声卡播放 PCM，两者都可以按通道做采样率转换。
接口声明在 `include/aio/ai_api.h`、`include/aio/ao_api.h`，实现在 `mpi/aio`，声卡访问由 ALSA 插件
`libaio_alsa_plugin.so`（`al/aio/alsa`）完成。

## 本版本的范围

已实现：

| 功能 | AI | AO |
| --- | --- | --- |
| 模块 | `AI_Init` `AI_DeInit` | `AO_Init` `AO_DeInit` |
| 设备属性 | `AI_SetDevAttr` `AI_GetDevAttr` | `AO_SetDevAttr` `AO_GetDevAttr` |
| 声卡选择 | `AI_SetMicAttr` `AI_GetMicAttr` | `AO_SetSpkAttr` `AO_GetSpkAttr` |
| 设备 | `AI_EnableDev` `AI_DisableDev` | `AO_EnableDev` `AO_DisableDev` |
| 通道 | `AI_EnableChn` `AI_DisableChn` `AI_QueryChnStat` | `AO_EnableChn` `AO_DisableChn` `AO_QueryChnStat` |
| 数据 | `AI_GetFrame` `AI_ReleaseFrame` | `AO_SendFrame` |
| 重采样 | `AI_EnableReSmp` `AI_DisableReSmp` | `AO_EnableReSmp` `AO_DisableReSmp` |

头文件里的其余接口（音量、静音、声道模式、编解码、VQE、AEC 参考、事件、存文件、通道参数、fd、
暂停/清空/排空等）尚未实现：调用时打印一条错误日志并返回 `SYS_ERR_INVAL`，不影响已有通道。
这些桩函数集中在 `mpi/aio/ai_stub.c`、`mpi/aio/ao_stub.c`。

## 构建

`mpi/aio` 随 `libspacemit_mpp.so` 一起编译，不依赖 ALSA。ALSA 插件只在找到 ALSA 开发文件
（`libasound2-dev`）时构建，找不到时跳过并在 cmake 输出里说明；此时 `AI_Init` / `AO_Init`
返回 `SYS_ERR_NOT_FOUND`。运行构建目录里的程序时用 `MPP_PLUGIN_DIR=<build>/lib` 指向插件，见
`al/readme.md`。

## 采集

```c
AioDevAttrS stAttr = {
    .eSampleRate = AUDIO_SAMPLE_RATE_16000, /* 必须是声卡支持的采样率 */
    .eBitWidth = AUDIO_BIT_WIDTH_16,
    .eSoundMode = AUDIO_SOUND_MODE_STEREO,
    .u32FrmNum = 4,          /* 声卡缓冲，单位是帧 */
    .u32PtNumPerFrm = 320,   /* 每帧点数，20 ms */
    .u32ChnCnt = 2,
};
MicAttrS stMic = {.stMicAttr = {.s32Dev = 2, .s32Chns = 2}}; /* 2 号声卡，见 /proc/asound/cards */
AudioFrameS stFrame;

AI_Init();
AI_SetMicAttr(0, &stMic);
AI_SetDevAttr(0, &stAttr);
AI_EnableDev(0);
AI_EnableChn(0, 0);
while (running) {
    if (AI_GetFrame(0, 0, &stFrame, NULL, 1000) != SYS_ERR_OK)
        continue;
    consume(stFrame.pu8Data, stFrame.u32Len); /* 指针只在 Release 之前有效 */
    AI_ReleaseFrame(0, 0, &stFrame, NULL);
}
AI_DisableChn(0, 0);
AI_DisableDev(0);
AI_DeInit();
```

- 帧的声道数、格式等于设备属性；`u64PTS` 是帧首样点的采集时刻（`CLOCK_MONOTONIC`，毫秒），
  `u32Seq` 连续递增。
- 每个通道最多排队 4 帧、最多同时借出 4 帧。队列满时丢弃最旧的未取帧，表现为 `u32Seq` 出现缺口，
  `AI_QueryChnStat` 的 `u64DroppedFrames` 增加。已借出的帧不会被覆盖。
- 同一设备可以使能多个通道，各自独立取帧；慢的通道只丢自己的帧。
- `pstAecFrame` 可以传非空，目前恒返回 `bValid = MPP_FALSE`。

完整示例：`sample/sample_ai_record.c`。

## 播放

```c
AO_Init();
AO_SetSpkAttr(0, &stSpk);
AO_SetDevAttr(0, &stAttr);
AO_EnableDev(0);      /* 先写入 u32FrmNum 帧静音，播放时钟从这里开始 */
AO_EnableChn(0, 0);
while (have_data) {
    fill(&stFrame);   /* 一帧 = 一个设备帧的时长；格式、声道数与设备一致 */
    AO_SendFrame(0, 0, &stFrame, AIO_BLOCK_INFINITE); /* 返回前已拷贝，缓冲可立即复用 */
}
AO_DisableChn(0, 0);
AO_DisableDev(0);
AO_DeInit();
```

- 发送队列深度是 4 × `u32FrmNum` 帧。队列满时：`AIO_NO_BLOCK` 返回 `SYS_ERR_FULL`，
  带超时返回 `SYS_ERR_TIMEOUT`，`AIO_BLOCK_INFINITE` 等到有空位。
- `AO_SendFrame` 成功只表示已入队。没有数据时设备播放静音而不会停，通道断流记入
  `AO_QueryChnStat` 的 `u64UnderrunPeriods`。这个计数目前是断流的**次数**，不是周期数，
  见「与头文件的已知差异」。
- 目前没有排空接口：发完后轮询 `AO_QueryChnStat` 直到 `u32ChnBusyNum` 为 0，再等
  `u32FrmNum + 2` 帧的时间，然后再停用。`u32FrmNum` 帧是声卡缓冲里还没播完的部分，
  多出的两帧是启用重采样时转换器里留着的尾巴。
- 轮询要有退出条件：声卡消失后队列不会再变空，`AO_QueryChnStat` 也不会报错。
  `AO_SendFrame` 已经返回 `AO_ERR_ABORTED`、或者 `u32ChnBusyNum` 超过一帧的时间没有减少，
  就不要再等。
- 每个 AO 设备同一时间只能使能一个通道（多路同时播放需要混音，尚未实现），第二个返回
  `SYS_ERR_BUSY`。

完整示例：`sample/sample_ao_play.c`。

## 重采样

声卡往往只支持一两种采样率。设备属性必须填声卡支持的采样率，应用需要的采样率在通道上声明：

```c
AI_EnableReSmp(0, 0, AUDIO_SAMPLE_RATE_16000); /* 该通道输出 16 kHz 的帧 */
AO_EnableReSmp(0, 0, AUDIO_SAMPLE_RATE_16000); /* 该通道接收 16 kHz 的帧 */
```

- 必须在对应的 `EnableChn` 之前调用；通道已使能时调用返回 `SYS_ERR_INVAL`。声明一直有效，
  直到 `DisableReSmp` 或 `DeInit`。
- 通道的每帧点数是设备每帧点数按采样率比例换算后四舍五入的值，恒定不变。例如设备 48 kHz、
  960 点，通道 16 kHz 时每帧 320 点。比例不是整数时（48 kHz、1024 点 → 16 kHz 为 341.33 点）
  每帧仍是 341 点，余数累积到后面的帧里，不丢样点；这时 AI 偶尔一个设备周期多出或少出一帧。
  选能整除的每帧点数可以避免这种节奏变化。
- `AO_SendFrame` 的帧必须正好是通道的每帧点数、采样率字段等于声明的采样率。
- 支持的格式：`S16_LE`、`S24_4LE`、`S32_LE`、`F32_LE`。其他格式（`U8`、`S8`、`S24_3LE`）可以
  原样采集和播放，启用重采样时 `EnableChn` 返回 `SYS_ERR_INVAL`。
- 换算后的每帧点数超过 `MAX_AIO_POINT_NUM` 或整帧超过 `MAX_AUDIO_FRAME_LEN` 时 `EnableChn`
  返回 `SYS_ERR_INVAL`。
- 转换器是加窗 sinc 的带限插值（`mpi/aio/aio_resample.c`），降采样时兼作抗混叠滤波。
  采集帧的 PTS 仍是样点的采集时刻，但帧比不重采样时晚大约一帧送达；播放同理多大约一帧延迟。

## 异常处理

- 声卡溢出/欠载（XRUN）或被系统挂起：设备线程自动恢复并打印限频告警。采集恢复后的第一帧带
  `AUDIO_FRAME_DISCONTINUITY`，PTS 按实际时间跳变；播放恢复时重新写入起始静音，队列里的数据不丢。
- 声卡消失（如 USB 声卡被拔出）：阻塞中的 `AI_GetFrame` / `AO_SendFrame` 返回
  `AI_ERR_ABORTED` / `AO_ERR_ABORTED`，之后的调用同样返回。不会自动重连，需要
  `DisableDev` 后重新 `EnableDev`。
- `EnableDev` 的失败原因：声卡不存在 `SYS_ERR_NOT_FOUND`；被其他进程或另一个逻辑设备占用
  `SYS_ERR_BUSY`；声卡不支持所设的采样率、声道数或格式 `SYS_ERR_INVAL`，日志里会列出声卡实际
  支持的范围。
- 声卡按自己的硬件周期工作，周期可能比一帧大（例如最小 512 点）。`u32FrmNum × u32PtNumPerFrm`
  必须不小于「一个硬件周期 + 两帧 − 周期与帧长的最大公约数」（周期等于帧长时就是两帧），
  否则缓冲撑不到声卡下一次唤醒，播放会反复断。不满足时 `EnableDev` 直接返回
  `SYS_ERR_INVAL`，日志里给出需要的点数。遇到时加大 `u32FrmNum` 或 `u32PtNumPerFrm`。
- `DisableChn` / `DisableDev` 会唤醒阻塞的调用（返回 `ABORTED`）。停用时仍未归还的采集帧
  保持有效，归还后才释放；`DeInit` 时仍未归还的帧被直接释放并告警。

## 声卡的打开方式

`s32Dev` 是 ALSA 声卡号，打开的是 `hw:<卡号>,0`：直接访问硬件、独占、不做任何隐式转换。
系统音频服务（PipeWire、PulseAudio）正在使用同一块声卡时 `EnableDev` 返回 `SYS_ERR_BUSY`。

环境变量 `MPP_AI_PCM` / `MPP_AO_PCM` 可以把采集 / 播放的 PCM 名字换成别的，例如
`MPP_AO_PCM=plughw:1,0` 让 alsa-lib 做格式和声道转换。setuid 程序忽略这两个变量。

## 与头文件的已知差异

下面的行为与 `include/aio` 头文件的字面定义不同，是有意保留的，条件具备后要改回来。

### `u64UnderrunPeriods` 按断流次数计

- **头文件的定义**：`AoChnStateS.u64UnderrunPeriods` 是通道在活动状态下断流的**周期数**，
  不含暂停和流结束之后（`ao_type.h`：「channel starved while active, excludes pause/EOS」）。
- **现在的实现**：每次由「有数据」变成「没数据」记 1 次，不论之后空了多少个周期。例如送入
  2 帧后停 8 个周期，头文件的定义是 8，现在得到 1。计数不为 0 仍然表示出现过断流。
- **原因**：本版本没有 `AO_SendEndOfStream`、`AO_DrainChn`、`AO_PauseChn`，框架分不清
  「应用来不及送数据」和「应用已经送完了」。按周期计的话，播放正常结束后计数会一直增长，
  这个数字就失去了意义。
- **什么时候改**：实现了流结束（`AO_SendEndOfStream` 或 `AO_DrainChn`）和暂停
  （`AO_PauseChn`）之后，改成通道活动期间每个空周期加 1，流结束之后和暂停期间不计。
- **改的时候要一起动的地方**：
  - `mpi/aio/ao.c` 的 `ao_chn_pull()`：两处 `u64Underruns++` 现在都由 `bPlaying` 把关，
    只在断流开始时计一次。
  - `test/aio/test_aio_ao.c`：`test_underrun` 和 `test_resample` 里对
    `u64UnderrunPeriods` 的预期值是按次数写的（1、2）。
  - `test/aio/test_aio_hw.c`：「the channel never starved while sending」这项检查读的是
    发送结束那一刻的计数，改成按周期计之后仍然成立，但发送结束后的读数会变。
  - 本节和「播放」一节里的说明。

## 测试

无硬件测试已注册到 CTest。它们把 `mpi/aio` 和一个可编程的假声卡后端（`test/aio/fake`）
链接在一起，由测试逐周期驱动并逐样点比对：

```sh
cmake -S . -B build -DBUILD_ROS2_EXAMPLES=OFF
cmake --build build --target test_aio_resample test_aio_ai test_aio_ao test_aio_concurrency test_aio_alloc
ctest --test-dir build -R '^test_aio_' --output-on-failure
```

| 测试 | 内容 |
| --- | --- |
| `test_aio_resample` | 转换器：全部采样率组合的样点数、分块无关性、通带、混叠抑制；PCM 格式换算 |
| `test_aio_ai` | AI 生命周期、取帧、丢帧、借出帧的所有权、重采样、XRUN 和设备丢失 |
| `test_aio_ao` | AO 生命周期、起始静音、逐比特播放、断流、帧校验、重采样、XRUN 和设备丢失 |
| `test_aio_concurrency` | 多线程取帧/送帧、阻塞调用与停用竞争、运行中反复启停 |
| `test_aio_alloc` | 使能路径上每个内存分配点和线程创建失败时的回滚；使能通道的瞬间设备丢失 |

真实声卡上的检查用 `test_aio_hw`（不注册到 CTest），用法见 `test/aio/test_aio_hw.c` 开头。
