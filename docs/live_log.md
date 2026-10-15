# MPP 运行时日志

MPP 程序在后台运行时，在另一终端执行 `mpp-log` 即可查看日志。
日志由目标进程通过本机 Unix domain socket 发送，显示在查看命令的终端。

## 快速使用

更新并重启使用新 `libmpp.so` 和新编解码/VI 插件的应用后：

```sh
mpp-log list
mpp-log -f
mpp-log -m vdec,al -l debug -f
mpp-log -m vdec,venc,al -l debug --tail 100 -f
mpp-log -m demux --tail 50
mpp-log -p 1234 -f
```

- `-p / --pid`：可选，只查看指定 PID；默认汇总同一 PID、网络命名空间内所有可访问的 MPP 进程。
- `-m / --module`：模块名，支持逗号分隔；默认 `all`。
- `-l / --level`：显示指定级别及更严重的日志，默认 `info`。
- `-f / --follow`：持续订阅，默认只显示连接后的新日志。未指定 `-p` 时每秒发现新进程；
  暂无进程时等待，单个进程退出不影响其他日志，服务重启或连接失败后自动重试。
- `-n / --tail`：每个进程先读取最近 N 条匹配且仍被保留的记录，范围 0～512。
- 不加 `-f` 时查询启动时发现的进程，默认每个进程读取最近 100 条后退出；
  未发现进程时提示并返回状态码 1。
- 多进程日志按接收顺序交错输出，每条保留 PID/TID，不做跨进程时间排序。
  丢日志等传输提示也标明来源 PID。指定 `-p` 后，目标进程断开即结束查看。
- 按 Ctrl+C 只退出查看命令，不停止目标进程。
- 每个终端独立过滤；断开连接自动撤销该终端的调试订阅。
- `mpp-log list` 显示已开启日志服务的进程；普通用户只能查看自己的进程，
  root 可以查看其他用户进程。查看端会检查目标 PID 和 socket 对端身份。

模块：

```text
sys vb vi vo uvc vdec venc demux mux v2d al utils cpp other
```

级别：

```text
fatal error warn info debug verbose
```

`vdec` 对应 MPI 解码日志，`al` 对应 Linlon 编解码插件及模块加载日志。
排查解码底层问题时通常同时订阅 `vdec,al`。VI 插件归入 `vi`。

输出示例：

```text
14:32:18.125 [INFO   ] [1234:1250] [vdec] vdec.c:320 VDEC_CreateChn: channel created
```

日志依次包含时间（毫秒）、级别、PID/TID、模块、文件/行号、函数及消息。
过长消息截断并标记 `[truncated]`；消息内换行转换为空格，保证一条记录一行。

## 接入与兼容性

- `SYS_Init()` 自动启动本进程的日志服务，最后一次对应的 `SYS_Exit()` 停止。
- 独立使用的 `DEMUX_Init()` 和 `MUX_Init()` 也会启动服务，因此纯 RTSP 拉流、
  封装应用无需为了查看日志而额外初始化 SYS；服务最晚在进程正常退出时清理。
- 其他不经过上述初始化的独立库使用者，可以调用 `mpp_log_start()` /
  `mpp_log_stop()`。启动幂等，失败返回 -1 并设置 errno；MPI 初始化不会因日志服务失败而失败。
- 初始化之前的日志可进入内存历史，服务启动后可以通过 `--tail` 查看。
- `error/info/debug`、`mpp_log*`、SYS/VB/UVC/DEMUX/MUX 的内部日志宏均走统一核心。
  `ENABLE_DEBUG` 不再裁剪日志，Release 构建也可动态打开 DEBUG。
- 旧日志函数符号及级别数值保留。`mpp_get_log_level()` 返回基础采集级别，不修改配置。
- 应用 sample/test 自己的 printf，以及显式调用的 SYS/VB/CPP 状态打印接口仍按原方式输出。
  此工具不拦截任意 stdout/stderr，也不采集内核驱动或第三方库的独立日志。

**输出行为变化：** 接入统一核心的日志默认写入进程内存，不再持续打印到原 stdout/stderr
或 syslog；通过 `mpp-log` 查看。已有应用和插件需一起更新并重启一次。
已在运行的旧版本进程不支持本接口。

## 采集规则和性能边界

默认基础采集级别 INFO，可用现有 `mpp_set_log_level()` API 调整。SILENT 关闭基础采集，
仍允许显式实时订阅临时开启指定模块。纯历史查询不会开启新的 DEBUG 采集。

实际采集级别是“基础级别”和该模块所有实时订阅所需级别中最详细的一个。
例如两个终端分别订阅 DEBUG 和 VERBOSE，关闭 VERBOSE 终端后仍保留 DEBUG；
最后一个调试订阅结束后恢复基础级别。API 查询返回基础级别，不返回订阅合成值。

- 每个进程固定保留 512 条记录，约 0.8 MiB；支持最多 8 个连接（含未完成握手的连接）。
- 未开启级别在宏入口过滤，不计算格式化参数。
- 生产线程使用 try-lock 入队，不等待 socket 或终端 I/O。竞争失败计数丢弃。
- 环形历史满后覆盖旧记录。每个订阅者有独立游标；慢读者收到
  `[mpp-log] dropped ...` 提示，其他读者继续工作。
- 覆盖计数表示整个记录序列的缺口，可能包含不匹配当前模块过滤条件的记录。
- 日志线程以有界批次发送，socket 为非阻塞 SOCK_SEQPACKET，避免半条记录或 SIGPIPE。
- 尚未发送请求的连接 3 秒超时释放。异常退出不会遗留 socket 文件。
- fork 后子进程关闭继承的服务连接并清空历史、订阅；调用上述 Init 或 start
  建立自己的服务，父进程继续工作。
- 此功能用于运行诊断，允许丢失，不用于审计。内存历史随进程退出丢失；
  退出时尚未送出的记录也不保证送达。不能补回此前未开启的 DEBUG 日志。

如需将查看到的日志保存为文件，可由命令行重定向：

```sh
mpp-log -m vdec,al -l debug -f > decode.log
```

## 构建和测试

日志测试源码统一放在 `test/log/` 目录下。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_ROS2_EXAMPLES=OFF
cmake --build build --target mpp mpp-log test_log_fixture -j4
ctest --test-dir build -R '^test_live_log' --output-on-failure
```

工具输出在 `build/utils/mpp-log`，CMake 安装目标将其安装到 `bin`。
插件仍需按项目原有方式构建；原有插件 POST_BUILD 脚本会尝试复制到 /usr/lib，
无权限时使用用户插件目录，验证构建应使用隔离环境。

`test_live_log` 不需要媒体硬件，覆盖 getter、关闭级别的参数不求值、errno、
真实 CLI、模块与级别过滤、历史查询、长日志、多读者、断连恢复、fork、
四线程生产、慢读者、错误请求、初始化/停止以及独立 DEMUX/MUX 接入。
`test_live_log_all` 覆盖无 PID 聚合、无进程等待、十个进程并发输出、动态发现、
逐进程历史条数、可选 PID 过滤、退出隔离、服务重启、握手拒绝/超时和全部订阅清理。
该测试串行运行；可在独立网络命名空间中运行日志测试，避免订阅板上的其他应用。
