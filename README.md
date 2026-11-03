# OpenLive Studio

基于 Qt6 的 Windows 桌面录制与直播工具。

## MVP 功能

1. **单画布**：唯一画布承载来源合成与预览。
2. **来源管理**：统一列表管理显示器采集、摄像头、图片。
3. **画面合成**：拖动/缩放/层级调整，简单预览。
4. **音频采集**：麦克风 + 系统声音混音，音量调节。
5. **本地录制**：输出 MP4。
6. **RTMP 推流**：填推流地址和密钥，开始/停止。
7. **编码**：优先 FFmpeg 软件编码，能加 NVENC/QSV 更好。

## 从哪里开始读

先读 [架构与阅读路线](docs/architecture.md)，再沿一条功能路径看代码，不必从底层采集 API 开始。

- `src/ui`：窗口、面板和用户操作。
- `src/video`：视频来源状态、采集控制和渲染快照聚合。
- `src/graphics`：GPU 资源互操作、画布渲染与合成。
- `src/audio`：设备采集、重采样和混音。
- `src/output`：音画时间对齐、编码、录制与直播生命周期。
- `src/util`：媒体时钟和输出订阅队列。

## 构建与验证

Windows 开发环境需要 MSVC、CMake、Qt 6.5+ 和共享版 FFmpeg 开发包（含 include、lib、bin）。开启测试时还需要 Qt Test 和 ffprobe。
在已配置 MSVC 的终端执行，替换下面的依赖路径：

```powershell
cmake -S . -B build/local -G Ninja -DCMAKE_PREFIX_PATH="<Qt目录>" -DFFmpeg_ROOT="<FFmpeg目录>" -DBUILD_TESTING=ON
cmake --build build/local --parallel
ctest --test-dir build/local --output-on-failure
```

测试按 `smoke`、`gpu`、`audio`、`output` 等标签组织，可以用 `ctest --test-dir build/local -L smoke --output-on-failure` 先验证基础行为。GPU、采集设备和直播集成测试依赖实际运行环境，阅读测试结果时也应检查跳过项。
