# OpenLive Studio

仅面向 Linux 的 Qt6 桌面录制与直播工具。已接入 V4L2 USB 摄像头、Wayland 显示器，以及 PulseAudio 麦克风和系统声音采集。

## MVP 功能

1. **单画布**：唯一画布承载来源合成与预览。
2. **来源管理**：统一列表管理显示器采集、摄像头、图片。
3. **画面合成**：拖动/缩放/层级调整，简单预览。
4. **音频采集**：麦克风 + 系统声音混音，音量调节。
5. **本地录制**：输出 MP4。
6. **RTMP 推流**：填推流地址和密钥，开始/停止。
7. **编码**：优先 FFmpeg 软件编码，能加 NVENC/QSV 更好。

## 从哪里开始读

先读 [架构与阅读路线](docs/architecture.md)，再沿一条功能路径看代码，不必从底层采集 API 开始。格式检查、分层测试与硬件验收入口见 [维护与验证](docs/maintenance.md)。

- `src/ui`：窗口、面板和用户操作。
- `src/video`：视频来源状态、采集控制和渲染快照聚合。
- `src/graphics`：OpenGL 画布渲染、像素转换与合成。
- `src/audio`：设备采集、重采样和混音。
- `src/output`：音画时间对齐、编码、录制与直播生命周期。
- `src/util`：媒体时钟和输出订阅队列。

## 构建与验证

构建需要 C++ 编译器、CMake 3.19+、pkg-config、Qt 6.5+（Core、Network、Widgets、Svg、OpenGL、OpenGLWidgets）和 FFmpeg 开发库。Wayland 显示器采集另需 Qt DBus 和 PipeWire 0.3.48+ 开发包（Ubuntu：`libpipewire-0.3-dev`，包含 SPA 头文件依赖）；音频采集需要 PulseAudio 13+ 客户端开发包（Ubuntu：`libpulse-dev`）。FFmpeg 以 7.1 系列为基线；开启测试时还需要 Qt Test 和 ffprobe。FFmpeg 6.1 及更早版本不满足要求，配置阶段会拒绝旧版库。

FFmpeg 通过 pkg-config 查找，不再使用 `FFmpeg_ROOT`。使用系统开发包时通常无需额外配置；使用用户目录内编译的 FFmpeg 时，在同一个终端先启用对应环境。以下示例对应安装在 `~/.local/ffmpeg-7.1` 的版本：

```bash
export PATH="$HOME/.local/ffmpeg-7.1/bin:$PATH"
export PKG_CONFIG_PATH="$HOME/.local/ffmpeg-7.1/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$HOME/.local/ffmpeg-7.1/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

pkg-config --modversion libavformat libavcodec libavutil libswresample libswscale
pkg-config --variable=prefix libavcodec
ffprobe -version
```

如果安装时已生成 `~/.local/ffmpeg-7.1/env.sh`，也可用 `source ~/.local/ffmpeg-7.1/env.sh` 代替上述三个 export。运行程序和测试时也应启用同一套动态库环境，不要混用不同安装位置的头文件和库。

指定 Qt 安装前缀后配置、构建和测试；Qt 位于系统搜索路径时可省略 `CMAKE_PREFIX_PATH`：

```bash
cmake -S . -B build/linux -DCMAKE_PREFIX_PATH="<Qt安装前缀>" -DBUILD_TESTING=ON
cmake --build build/linux --parallel 2
ctest --test-dir build/linux --output-on-failure
```

V4L2 摄像头使用系统内核接口，支持离散模式的单平面 USB 视频采集，无需安装 libv4l。音频采集通过 PulseAudio 客户端连接桌面音频服务，支持原生 PulseAudio 和提供 PulseAudio 协议的兼容服务（如 pipewire-pulse），无需更换系统音频服务。真实 GPU、采集和系统授权测试需要图形桌面与实际设备。

Wayland 显示器采集依赖运行中的 PipeWire、`xdg-desktop-portal` 及与当前桌面匹配的 ScreenCast 后端。添加来源时点击“选择显示器…”（或“确定”），在系统弹窗选择一台显示器并授权；收到首帧后才创建来源，取消不会留下空来源。画面以原始分辨率、SDR、最高 30 FPS 通过 CPU 像素帧进入现有合成链路，预览、滤镜、变换、录制和推流共用同一来源。首版性能目标为单显示器 1080p，不承诺更高分辨率的帧率。

鼠标默认开启，系统不支持的模式会禁用并说明实际状态。隐藏来源会释放共享会话，再次显示重新选择；删除来源或退出会关闭会话。系统停止共享、视频连接错误会清空该来源画面，并显示“重试”按钮；不自动弹窗重试。不保存屏幕授权，不实现跨重启恢复、窗口采集、区域裁剪、X11 专用后端或 GPU 零拷贝。

在混音面板添加“麦克风”或“系统声音”后立即开始采集。设备列表默认选择“跟随系统默认输入/输出设备”，也可固定选择某个设备；系统声音采集该输出设备播放的全部声音。默认设备变化后自动切换，固定设备断开则等待原设备恢复；设备或 PulseAudio 服务恢复后自动重连，期间混音补静音，录制和直播继续。每路音量、静音和电平沿用现有混音链路，不提供麦克风监听，也不保存跨重启来源配置。

`audio.sources` 验证重采样、混音和来源管理。`audio.pulseaudio` 使用独立的 PulseAudio 服务、虚拟麦克风和输出设备验证真实采样接口、44.1 kHz 输入转换、默认设备切换、设备删除/重建、服务重启，以及 MP4 和本机 RTMP 接收文件中可解码的非静音音频；它不访问桌面音频设备。该测试通过 `dbus-run-session` 使用独立会话总线，还需要 `pulseaudio`、`pactl` 和 `paplay`（Ubuntu：`pulseaudio pulseaudio-utils`），缺少工具时跳过。没有实际设备时不提供虚假的默认设备选项。

```bash
ctest --test-dir build/linux-ffmpeg71 -L audio --output-on-failure
OPENLIVE_AUDIO_HARDWARE=1 build/linux-ffmpeg71/tests/tst_audio capturesHardwareMicrophoneAndSystemAudio hardwareFaultRecoveryAndIndependentSources
```

第二条命令需要实际 PulseAudio 音频设备。虚拟设备测试与注入故障不能替代真实麦克风收音、耳机/蓝牙切换、物理拔插及长期音画同步验收。

`capture.portal` 测试通过 `dbus-run-session`（测试依赖）运行模拟系统服务，验证授权请求、取消和资源释放。真实 Wayland 桌面的多屏选择、鼠标、显隐和删除，以及 1080p 录制/推流仍需实机验收；模拟服务和离屏测试不能替代这些检查。

测试按 `smoke`、`gpu`、`audio`、`output` 等标签组织，可以用 `ctest --test-dir build/linux -L smoke --output-on-failure` 先验证基础行为。GPU、采集设备和直播集成测试依赖实际运行环境，阅读测试结果时也应检查跳过项。
