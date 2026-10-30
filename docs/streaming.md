# RTMP 推流

当前本机测试结果见[推流综合验收](streaming-acceptance.md)。

## 链路和独立启停

`Compositor` 和 `AudioMixer` 各自提供有界订阅队列。`RecordingController` 和 `StreamingController` 各持有一组订阅，由独立后台线程消费；停止、重连和销毁只释放自己的订阅。预览及来源采集继续工作，最后一个画面订阅释放后不再持续 CPU 读回。

`OutputTimeline` 复用录制的时间线处理：QPC 单调时钟、各输出独立起点、约 200 ms 调度缓冲、最近画面采样、每帧对应 1600 个双声道音频采样、缺音补零。两路分别使用 `MediaEncoder` 实例，复用编码代码，不共享编码器或文件/网络 I/O。

固定输出为 H.264、YUV420P、BT.709 limited、1920×1080、30 FPS，GOP 60、无 B 帧；音频为 AAC、48 kHz、双声道、192 kbps。录制默认 12000 kbps，直播默认 6000 kbps，视频码率允许 500–50000 kbps。直播使用受 VBV 约束的 CBR，录制使用受约束的平均码率，实际文件码率依画面复杂度变化。软件编码使用 x264，两路均为 ultrafast、zerolatency；每实例使用 4 个编码线程，避免双路过度创建线程；录制工作线程使用 Qt HighPriority，以落实录制优先的调度策略。直接转换原生 FBO 像素，避免双路重复进行整帧通道转换。

直播封装为 FLV 后通过 FFmpeg 原生 RTMP 输出；录制仍通过 Qt Unicode 文件 I/O 创建 MP4，拒绝覆盖已有文件。

## 状态、恢复和边界

- 状态为 `idle → connecting → live`；临时网络故障进入 `reconnecting`；用户停止经 `stopping` 返回 `idle`；明确配置、编码及共享来源错误进入 `error`。
- 失败等待间隔为 1、2、5、10 秒，此后每次 10 秒，直到恢复或用户停止。直播时长从首次连接并就绪开始累计，包含重连等待。连接成功不代表观众端已经播放。
- DNS 异步解析并具有取消及超时；实际协议连接使用解析后的 IP，并保留原始 RTMP tcUrl。连接、握手及网络写入通过 FFmpeg interrupt callback 和 `rw_timeout` 限制，单次网络操作预算为 3 秒。停止可以中断等待和网络 I/O。
- 画面每订阅最多 32 幅、混音每订阅最多 100 个 10 ms 块；不会建立无限增长的编码包队列。网络发送持续落后一秒时丢弃本次直播会话并重新连接。瞬时编码/调度抖动导致落后时，在当前连接内跳过过期画面和音频，推进两路 PTS 并强制新 IDR；不编码追赶旧画面的突发流量。
- 最近十秒内的编码处理平均耗时持续超过每帧预算（至少观察三秒，并持续超预算两秒）时停止直播，明确提示性能不足，不作为网络故障重试。录制积压超过 650 ms 且在 150 ms 观察窗内不再回落时，优先停止直播释放资源；正在消退的短暂调度抖动继续恢复；无法保证整机性能不足时录制仍然成功。
- 网络、文件及两路编码实例的故障独立；合成器、混音器和来源设备仍是共享资源，它们的故障可能影响两路。
- 参数校验、权限错误及收到的明确 RTMP 拒绝不重试。某些服务器直接断开连接而不返回拒绝原因，例如当前 SRS 的同名流占用；客户端无法可靠判断原因，按临时连接故障重试，用户可以停止。

连接中、直播中和重连期间锁定服务器、密钥及直播码率；录制期间只锁定保存目录和录制码率。另一条未运行输出的配置和启动不受限制。设置只保存在本次运行内，不写入配置文件或系统凭据存储。

控制区可以开始/停止直播、取消连接、停止重连。状态栏右侧仅显示录制和直播的时长，未开始及停止完成后显示 `00:00:00`；直播工具提示保留最近错误，状态栏左侧仍显示错误提示。实际发送码率、跳过画面数及重连次数保留在控制器中，不再显示于状态栏。发送码率统计 FFmpeg 成功写出的媒体字节，不等同于网络链路带宽或服务器接收确认。密钥默认遮蔽，FFmpeg 原始诊断可能包含服务器响应，因此统一转换为无地址和密钥的错误信息。

## SRS 联调

本机虚拟机已有 SRS 6.0.193 容器，无需重复安装或更改配置。RTMP 1935 和 HTTP 8080 可达，API 1985 仅监听虚拟机 loopback。现有服务启用了 HTTP-FLV 和 HLS。

在设置中填写服务器 `rtmp://192.168.195.130/live`，串流密钥填写自选测试流名。HTTP-FLV 播放地址为 `http://192.168.195.130:8080/live/<流名>.flv`。首版只接受 RTMP；RTMPS、第三方平台登录和多目标推流尚未实现。

## 验证和复现

录制回归另注入单次 550 ms 调度暂停，验证在约一秒的有界画面历史中恢复，不误触发停播；持续编码过载仍必须结束录制并尽力完成文件。音频时间线拷贝和混音使用连续样本缓冲，避免逐采样容器索引的额外开销。音频采集和混音线程使用 HighPriority；混音允许最多 100 ms 的短暂补算，连同 60 ms 输入抖动缓冲仍位于 200 ms 输出调度窗口内，避免原先超过 40 ms 就跳过时间段而产生静音缺口。

普通 CTest 的 `output.streaming` 验证配置、独立锁定、独立订阅，以及不响应握手的 TCP 服务下的超时、重连和取消；测试期间本地录制必须连续。SRS 集成和长时间验收默认跳过，需要显式指定环境变量。

```powershell
$env:PATH = 'D:/DevEnvs/Qt/6.12.0/msvc2022_64/bin;D:/DevEnvs/ffmpeg-9.0.2-full_build-shared/bin;' + $env:PATH
$env:QT_PLUGIN_PATH = 'D:/DevEnvs/Qt/6.12.0/msvc2022_64/plugins'
$env:OPENLIVE_SRS_SERVER = 'rtmp://192.168.195.130/live'
./build/recording-compositor-Debug/tst_streaming.exe srsIndependentLifecycle srsTemporaryEncoderPauseKeepsConnection srsNetworkFaults -o srs-results.txt,txt
```

长测生成合成画面、同钟闪光与 1500 Hz 音频脉冲，双路同时编码。画面下半部携带 100 ms 精度的单调时钟，独立 FFmpeg HTTP-FLV 解码器据此测量接收端延迟；播放器限定为无缓冲、低延迟模式，解码和滤镜线程受限。此结果覆盖编码、服务器和解码路径，不包含显示器扫描输出延迟，也不代表任意播放器的默认缓冲。

```powershell
$env:OPENLIVE_SRS_KEY = 'openlive-endurance'
$env:OPENLIVE_SRS_HTTP = 'http://192.168.195.130:8080/live/openlive-endurance.flv'
$env:OPENLIVE_STREAMING_SOAK_SECONDS = '1800'
$env:OPENLIVE_STREAMING_ARTIFACTS = 'D:/Code/OpenLiveStudio/build/streaming-acceptance-final'
$env:QTEST_FUNCTION_TIMEOUT = '2400000'
./build/recording-compositor-Debug/tst_streaming.exe srsEndurance -o streaming-acceptance-results.txt,txt
```

可在第二个 PowerShell 窗口运行资源和首尾样本监视器：

```powershell
./tools/monitor-streaming.ps1 -ArtifactsDirectory 'D:/Code/OpenLiveStudio/build/streaming-acceptance-final' -PlaybackUrl 'http://192.168.195.130:8080/live/openlive-endurance.flv' -FFmpeg ffmpeg
```

监视器只读取测试进程和状态，每十秒保存私有内存、工作集、句柄和 CPU 时间，并在开始及接近结束时无重编码保存 HTTP-FLV 样本；不会停止进程或更改服务器。

结果目录必须为空，已有 `endurance.mp4` 不会被覆盖。`progress.json` 和 `summary.json` 保存状态、时长、帧数、重连、混音缺失、播放延迟和测试主循环心跳。测试主循环还执行进度文件写入与管道采样，心跳峰值仅作诊断，不能等同于产品 UI 阻塞；产品 UI 的取消连接和关闭等待由原生窗口测试独立验证。长测期间不要并发执行其他编码压力测试。

可用 FFmpeg 从 HTTP-FLV 无重编码保存一段样本，再对 MP4 或 FLV 检查同步：

```powershell
ffmpeg -v error -i $env:OPENLIVE_SRS_HTTP -t 20 -c copy sample.flv
python tools/measure-stream-sync.py sample.flv --ffmpeg ffmpeg --ffprobe ffprobe --report sync.json
```

验收目标为双路至少 30 分钟、播放延迟不超过 3 秒、音画偏差不超过 150 ms。网络故障测试另覆盖断连、恢复、TCP 背压和录制连续性；另注入 1.2 秒直播编码暂停，验证连接不变、跳帧后关键帧、音视频 DTS 单调及实际解码；原生 UI 测试 `streamingControlsAndGracefulClose` 覆盖设置锁定、重连期间可操作、两路同时关闭及解除锁定。真实屏幕、摄像头和声音设备的既有录制验收见[录制综合验收](recording-acceptance.md)。

实现依据：[FFmpeg 网络超时与 RTMP 选项](https://ffmpeg.org/ffmpeg-protocols.html)、[AVIO 可中断接口](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/avio.h)。

SRS 默认 full 时间戳校正可能压缩较大时间跳变，接收端不一定保留发送端的时间间隙；验证同时检查发送前关键帧标志、接收端 DTS 单调和实际解码。[SRS 时间戳校正说明](https://ossrs.io/lts/en-us/docs/v8/doc/rtmp)。
