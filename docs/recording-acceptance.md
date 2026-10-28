# 录制综合验收

`MainWindowTest::recordingAcceptance` 通过真实主窗口录制按钮，运行屏幕采集、摄像头、麦克风和系统声音，同时加入图片与同步校准信号。测试默认跳过，需要明确设置时长。测试文件保存在构建目录内的 `acceptance-日期时间-随机值/`，包括 MP4、`progress.json` 和 `summary.json`；文件可能包含本机画面、摄像头和声音，请在本地审阅。

## 运行

在可访问交互桌面、桌面 OpenGL、摄像头和音频设备的 Windows 环境执行。先完成构建，并把 Qt、FFmpeg 的 bin 目录加入 PATH，配置 Qt 插件及 QML 路径。例如 PowerShell：

```powershell
$env:PATH = 'D:/DevEnvs/Qt/6.12.0/msvc2022_64/bin;D:/DevEnvs/ffmpeg-9.0.2-full_build-shared/bin;' + $env:PATH
$env:QT_PLUGIN_PATH = 'D:/DevEnvs/Qt/6.12.0/msvc2022_64/plugins'
$env:QML_IMPORT_PATH = 'D:/DevEnvs/Qt/6.12.0/msvc2022_64/qml'
$env:OPENLIVE_RECORDING_SOAK_SECONDS = '600'
$env:QTEST_FUNCTION_TIMEOUT = '1200000'
./build/recording-compositor-Debug/tst_mainwindow.exe canShowAndClose recordingAcceptance -o acceptance-results.txt,txt
Remove-Item Env:OPENLIVE_RECORDING_SOAK_SECONDS
Remove-Item Env:QTEST_FUNCTION_TIMEOUT
```

路径需按本机安装调整。短预检可使用 12 秒；正式验收必须真实运行 600 秒。`canShowAndClose` 消耗 Windows 首次隐藏窗口启动状态，保证后续窗口能够正常展示。验收要求实际设备存在并成功采集，缺少设备时会失败，不计为通过。

## 检查内容与标准

- 录制中修改图片位置、旋转和色彩滤镜，删除后重新添加图片，锁定后拒绝变换；摄像头隐藏时释放采集资源，恢复后继续工作。
- 窗口大部分时间最小化，期间仍持续录制；还原后调整大小，再次最小化。
- 麦克风故障注入实际关闭指定采集会话，系统声音保持工作；取消注入后原端点恢复。再验证静音、音量、删除并重新添加系统声音。
- 停止后来源和输出资源正常释放；ffprobe 检查 1080p、30 FPS、音视频时长差小于 35 ms。ffmpeg 独立解码全部视频及音频，解码帧数必须等于编码统计。
- 同步信号使用同一单调时钟，在实际 GUI 发布闪光时同时标记声音脉冲起点；声音在独立线程按 48 kHz 生成。编码后按闪光上升沿和 1500 Hz 窄带音频脉冲配对，最大偏差不超过 100 ms，首尾各最多 30 次事件的平均偏差变化小于 50 ms。该方法覆盖来源发布到合成、混音、编码和封装的时间关系。
- GUI 心跳最大间隔小于 250 ms；一分钟预热后的私有内存增长小于 192 MiB、句柄增长小于 64；混音输出缺失采样数必须为零。

`output.recording` 另通过 `OPENLIVE_RECORDING_ENCODER_DELAY_MS` 注入编码过载，检查缓冲受限、报错后已有 MP4 可播放，以及随后重新录制能够成功。故障注入环境变量只用于测试，不由产品界面设置。

## 本机结果（2026-10-28）

Windows 10、NVIDIA GeForce RTX 4060 Ti、Qt 6.12.0 / MSVC Debug、FFmpeg 9.0.2，真实 WGC 屏幕、Media Foundation 摄像头、WASAPI 麦克风及系统声音同时开启。首轮发现渲染后时间戳和向前取帧造成的额外视频延迟；修正快照时间戳并采用最近帧后，重新完整运行十分钟，结果通过。

| 项目 | 最终测量 |
| --- | --- |
| 文件时长 / 解码帧数 | 600.1 秒 / 18,003 帧，与编码统计一致 |
| 同步事件配对 | 598 组 |
| 最大绝对音画偏差 | 90 ms |
| 首 30 / 尾 30 组平均偏差 | -23.44 / -24.11 ms（负值表示声音较早） |
| 首尾平均偏差变化 | 0.67 ms |
| 混音输出缺失采样 / 输出队列丢帧 | 0 / 0 |
| 固定帧率重采样中的重复帧 | 253 帧，约 1.4% |
| GUI 最大心跳间隔 | 219 ms |
| 预热后私有内存基线 / 峰值 | 1,264,689,152 / 1,313,861,632 字节，增加约 46.9 MiB |
| 预热后句柄基线 / 峰值 | 1,182 / 1,192 |

图片变换与滤镜、摄像头显隐和资源释放、图片替换、麦克风故障恢复、静音与音量、声音来源删除重加、窗口最小化/还原/缩放、来源锁定以及结束后的资源释放均通过。QtTest 本次 4 项通过、0 失败、0 跳过（含初始化、窗口启动与清理）。

最终 CTest 回归 10/10 组通过；录制组 7 项通过且无跳过，包含过载终止与再次录制。另行运行原生窗口设置、录制控制和关闭等待测试，5 项通过、0 失败、0 跳过；普通 offscreen UI 测试按设计跳过需要桌面或显式开启的硬件/长时项。

本地原始结果：`build/recording-compositor-Debug/acceptance-20261028-211351-480760/summary.json`；录制文件在同一目录；测试日志为 `build/recording-compositor-Debug/tests/acceptance-ten-minute-final.txt`。这些生成文件不纳入 Git。

## 覆盖边界

同步校准信号直接进入画面来源和混音入口，不测量摄像头曝光、麦克风硬件及驱动的物理端到端延迟。实际 USB 拔插、其他厂商/虚拟设备、其他机器及一小时以上运行仍需对应实机验收。音频故障注入不能替代物理拔插测试。
