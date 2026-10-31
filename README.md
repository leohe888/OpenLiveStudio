# OpenLive Studio

基于 Qt6 的 Windows 桌面录制与直播工具，界面全部采用 Qt Widgets，以 C++ 布局编写。界面固定使用浅色配色，样式集中定义在 `assets/styles/style.qss`，不提供主题切换。

## 目录结构

- `src/core/`：跨模块基础设施，如媒体时钟和输出订阅队列。
- `src/video/`：画面源数据、来源管理、变换和滤镜；`image/`、`display/`、`camera/` 分别存放对应来源的控制器与采集实现。
- `src/audio/`：声音来源管理、音频采集、重采样和混音。
- `src/graphics/`：画面合成、渲染、共享帧和 GPU 资源交接。
- `src/output/`：编码、录制、推流和输出设置。
- `src/ui/`：主窗口、OpenGL 预览控件和 Widgets 面板及对话框。
- `tests/`：按 `video/`、`audio/`、`graphics/`、`output/`、`ui/` 分组，公共测试辅助代码放在 `support/`。
- `assets/`：图标和着色器；`docs/`：功能与验收说明；`tools/`：开发和验证脚本。

C++ 项目头文件统一以 `src/` 为引用根目录，例如 `#include "video/videosourcemanager.h"`。

界面使用 C++ 与 Qt 布局管理器构建，不使用 `.ui` 文件。`sourcewidgets`、`audiowidgets`、`outputwidgets` 分别负责画面来源、声音来源和输出控制界面，复用画面/声音来源管理类与控制器。样式由启动时加载的 `assets/styles/style.qss` 统一控制，操作图标使用 `assets/icons/` 中的 Lucide SVG；预览保留 `QOpenGLWidget`。

`VideoSourceManager` 是普通的 `QObject`，通过 `sources()` 和 `source(id)` 提供只读的 `VideoSource` 数据。来源列表的增删和排序触发 `sourcesChanged`；单个来源的配置及状态变化分别触发 `sourceChanged` 和 `sourceStatusChanged`，面板按来源 ID 更新对应行。

`AudioSourceManager` 同样是普通的 `QObject`，提供 `AudioSource` 数据快照和按 ID 查询。混音面板仅在来源增删时重建通道，名称、音量、静音及设备状态通过 `sourceChanged` 更新，左右电平通过独立的 `sourceLevelsChanged` 更新。工作线程和音频队列由管理类私有持有。

`OutputSettings` 通过普通 C++ 方法提供录制和推流配置，保留独立的录制/推流锁定信号供设置对话框更新可编辑状态。设置仅在本次运行内生效，应用失败时保留原配置，录制目录只在开始录制时创建。

控制器通过普通 C++ 方法和信号槽与 Widgets 交互。设备目录使用 `DisplayTarget`、`CameraDevice`、`AudioDevice`，摄像头模式使用 `CameraMode`，应用摄像头配置时传递 `CameraConfig`；来源属性通过 `cameraSource(id)`、`displaySource(id)` 读取具体配置和运行状态。保留的颜色 `Q_PROPERTY` 用于 QSS，`CameraMode` 元类型用于 `QComboBox` 的项数据，不依赖 QML。

QSS 编入 Qt 资源，修改后重新构建并启动程序即可生效。预览的 `qproperty-selectionColor`、`qproperty-surroundingColor` 和电平条的 `qproperty-normalColor`、`qproperty-warningColor`、`qproperty-clippingColor` 也在同一文件配置；声音通道使用 `[selected="true"]` 表示选中状态。

## MVP 功能

1. **单画布**：唯一画布承载来源合成与预览。
2. **来源管理**：统一列表管理显示器采集、摄像头、图片。
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
