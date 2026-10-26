# MPP 系统日志

MPP 统一日志通过 `/dev/log` 发送给系统 syslog，标识为 `mpp`。
日志保存、过滤和轮转由系统日志服务负责；发送失败时自动回退到 stderr。

## 控制采集级别

默认 INFO，输出 INFO、WARN、ERROR、FATAL。启动应用时可以设置：

```sh
MPP_LOG_LEVEL=debug mpv video.mp4
MPP_LOG_LEVEL=error ./your_app
MPP_LOG_LEVEL=silent ./your_app
```

支持 `fatal/error/warn/info/debug/verbose/silent`，名称不区分大小写，
也支持对应的 MPP 数值 `1/2/3/4/5/6/7`。环境变量无效时回退到 INFO。
这里的数值是 MPP 的级别，不是 syslog 的优先级。

运行中通过普通配置文件调整所有使用默认路径的 MPP 进程，无需重启应用。
以有权写入 `/run/mpp` 的用户执行：

```sh
mkdir -p /run/mpp
echo debug > /run/mpp/log-level  # 开启 DEBUG
echo info > /run/mpp/log-level   # 恢复 INFO
echo silent > /run/mpp/log-level # 关闭日志
rm /run/mpp/log-level           # 恢复各进程的 API / 环境变量等级
```

文件只包含一个等级，允许首尾空白。有效文件配置优先于 API 和环境变量。
每个进程在日志调用或查询等级时最多每秒检查一次；空闲时不轮询，
下一次调用在检查间隔到期后读取新值，SILENT 也可重新开启。
空内容、无效值、读取失败或非普通文件保留上次有效配置；没有有效配置时使用进程基准等级。
文件删除后恢复基准等级。`/run` 中的配置重启系统后消失，MPP 不自动创建该文件或目录。

程序内可随时设置当前进程的基准等级：

```c
mpp_set_log_level(MPP_LOG_DEBUG);
int level = mpp_get_log_level(); // 返回当前实际生效的等级
mpp_set_log_level(MPP_LOG_INFO);
```

API 设置优先于环境变量；文件覆盖期间 API 设置仍被保留，删除文件后生效。
环境变量仅用于初始化基准等级，从另一终端修改它不会改变已经运行的进程。
级别在格式化前检查，被关闭的日志不计算参数。Release 构建同样支持 DEBUG。

## 实时查看和历史查询

使用 systemd journal 的系统：

```sh
journalctl -t mpp -f                   # 实时查看 MPP 日志
journalctl -t mpp -f -p warning        # 仅查看 WARN 及以上
journalctl -t mpp --since "10 min ago" # 查询历史
```

查看器筛选的是已经产生的记录，不能通过它打开 MPP 内部关闭的 DEBUG。
MPP 的 VERBOSE 和 DEBUG 都映射为 syslog DEBUG，FATAL 映射为 CRIT。

其他系统使用自身的 syslog 查看工具或跟随日志文件，例如：

```sh
tail -F /var/log/syslog
```

实际文件可能是 `/var/log/messages`，也可能只保存在 journal；
位置、持久化方式、保留期限和读取权限均以系统配置为准。
syslog 接收端缺失、拒绝接收或队列拥塞时，当前日志自动写入 stderr，无需额外开关。
回退遵循相同的等级过滤，`silent` 时不输出；后续发送成功后只使用 syslog。

通过等级过滤的日志逐条输出到 stderr。
没有系统日志服务时，可以由应用启动环境保存 stderr，例如：

```sh
./your_app 2>>mpp.log
```

回退同步写入应用的 stderr，不修改其文件描述符属性或进程信号处理设置。
阻塞的管道或终端可能使回退写入等待；stderr 不可写时允许丢失，不保证持久化。

## 行为和边界

- 所有接入统一日志核心的 MPP 模块使用相同的等级控制和 syslog 通道。
- 不写应用的 stdout；syslog 发送成功时不重复写 stderr，不修改宿主应用的 `openlog()` 配置。
- 日志包含时间、级别、PID/TID、模块、文件行号和函数；超长消息截断并标记，
  内部换行转为空格。系统日志服务另行记录日期等元数据。
- 日志通过非阻塞 datagram 发送，支持多线程并发调用。
- 成功恢复 syslog 发送后尝试报告累计未送达 syslog 的数量，其中包括已经回退到 stderr 的记录。
  该统计不能覆盖接收端过滤、限流或写盘失败，也不保证断电不丢失。
- `MPP_SYSLOG_SOCKET=/absolute/path` 可指定其他本机 syslog 接口，默认 `/dev/log`。
- `MPP_LOG_LEVEL_FILE=/absolute/path` 可指定其他等级配置文件，例如隔离测试。
  三个环境变量都在提权执行时忽略；配置文件不接受末级符号链接。
- sample/test 的普通 printf、显式状态打印、内核及第三方库独立日志不被拦截。

## 验证

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_ROS2_EXAMPLES=OFF
cmake --build build --target mpp test_log_fixture -j4
ctest --test-dir build -R '^test_log_syslog$' --output-on-failure
```

测试位于 `test/log/`，使用私有 socket 和配置文件，不依赖媒体硬件或系统日志服务。
覆盖环境配置、文件动态调级及删除回退、API 优先级、SILENT 恢复、无效配置、
关闭日志参数不求值、errno、消息格式、多进程、fork、并发发送、
接收端缺失/拒收/拥塞、stderr 逐条回退、断开的管道及恢复后的发送统计。
