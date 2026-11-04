# OpenLive Studio

基于 Qt6 的桌面录制与直播工具。当前分支正在向 Linux 移植，采集后端尚未实现；范围与验收标准见 [Linux 移植规格](docs/linux-port-spec.md)。

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

构建需要 C++ 编译器、CMake 3.19+、pkg-config、Qt 6.5+（Core、Network、Widgets、Svg、OpenGL、OpenGLWidgets）和 FFmpeg 开发库。FFmpeg 以 6.1 系列为基线；开启测试时还需要 Qt Test 和 ffprobe。Ubuntu 22.04 自带的 FFmpeg 4.4 不满足要求，配置阶段会拒绝旧版库。

FFmpeg 通过 pkg-config 查找，不再使用 `FFmpeg_ROOT`。使用系统开发包时通常无需额外配置；使用用户目录内编译的 FFmpeg 时，在同一个终端先启用对应环境。以下示例对应安装在 `~/.local/ffmpeg-6.1` 的版本：

```bash
export PATH="$HOME/.local/ffmpeg-6.1/bin:$PATH"
export PKG_CONFIG_PATH="$HOME/.local/ffmpeg-6.1/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$HOME/.local/ffmpeg-6.1/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

pkg-config --modversion libavformat libavcodec libavutil libswresample libswscale
pkg-config --variable=prefix libavcodec
ffprobe -version
```

如果安装时已生成 `~/.local/ffmpeg-6.1/env.sh`，也可用 `source ~/.local/ffmpeg-6.1/env.sh` 代替上述三个 export。运行程序和测试时也应启用同一套动态库环境，不要混用不同安装位置的头文件和库。

指定 Qt 安装前缀后配置、构建和测试；Qt 位于系统搜索路径时可省略 `CMAKE_PREFIX_PATH`：

```bash
cmake -S . -B build/linux -DCMAKE_PREFIX_PATH="<Qt安装前缀>" -DBUILD_TESTING=ON
cmake --build build/linux --parallel 2
ctest --test-dir build/linux --output-on-failure
```

这里只说明 Linux 构建依赖接入，不表示显示器、摄像头和音频采集已经支持 Linux。真实 GPU、采集和系统授权测试需要图形桌面与实际设备。

测试按 `smoke`、`gpu`、`audio`、`output` 等标签组织，可以用 `ctest --test-dir build/linux -L smoke --output-on-failure` 先验证基础行为。GPU、采集设备和直播集成测试依赖实际运行环境，阅读测试结果时也应检查跳过项。
