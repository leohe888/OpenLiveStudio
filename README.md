# OpenLive Studio

基于 Qt6 的 Windows 桌面录制与直播工具，采用 QML + Qt Widgets 混合界面。界面固定使用浅色配色，颜色集中定义在 `src/core/uicolors.h`，不提供主题切换。

## MVP 功能

1. **单画布**：唯一画布承载来源合成与预览。
2. **来源管理**：统一列表管理显示器采集、窗口采集、摄像头、图片、文字。
3. **画面合成**：拖动/缩放/层级调整，简单预览。
4. **音频采集**：麦克风 + 系统声音混音，音量调节。
5. **本地录制**：输出 MKV/MP4。
6. **RTMP 推流**：填推流地址和密钥，开始/停止。
7. **编码**：优先 FFmpeg 软件编码，能加 NVENC/QSV 更好。
8. **快捷键**：开始/停止录制。
9. **断线重连/异常恢复**：推流断开重连，MKV 崩溃可修复。

## 已实现的采集链路

- [显示器采集](docs/display-capture.md)：WGC 与 GPU 纹理交接。
- [摄像头采集](docs/camera-capture.md)：Media Foundation 异步视频采集、模式设置和故障恢复。
- [独立画布合成](docs/compositor.md)：后台合成、共享纹理输出，以及隐藏和最小化时持续输出。
- [声音来源与混音](docs/audio-sources.md)：WASAPI 麦克风与系统声音、多设备管理、电平和后台混音。
- [MP4 录制](docs/recording.md)：后台 H.264/AAC 编码、统一时间线和正常停止封装。
- [RTMP 推流](docs/streaming.md)：独立编码和输出、自动重连、实时播放及双路独立启停。
