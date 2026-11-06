# 架构与阅读路线

## 先建立一条数据流

```text
用户操作 → MainWindow → 来源管理器 / 来源控制器
视频设备 / 图片 → RenderSourceSnapshot 快照 → VideoSourceAggregator
    → PreviewWidget → Compositor / CanvasRenderer → 预览
                                             → 视频输出订阅 ┐
音频设备 → AudioCaptureWorker → AudioMixer → 音频输出订阅 ───┤
                                                          ↓
                                     OutputTimeline → MediaEncoder
                                                          ↓
                                                 MP4 / RTMP
```

这里有两类数据：来源名称、可见性、变换等是控制状态；视频帧、音频采样是媒体数据。管理器负责状态，控制器负责设备生命周期，工作线程负责持续处理媒体。不要把“来源列表发生变化”和“设备产生了一帧”理解为同一种事件。

## 推荐阅读顺序

1. 从 `src/ui/mainwindow.cpp` 的构造函数开始：来源初始化、预览、输出控制器、错误提示、面板。这里负责组装各模块。
2. 看 `src/video/videosource.h` 和 `videosourcemanager.h`：理解来源的状态和允许的操作。再看图片控制器，沿“添加图片”走到 `VideoSourceAggregator::publishRenderSources()`。
3. 看 `src/graphics/rendersourcesnapshot.h`、`src/ui/previewwidget.cpp` 和 `src/graphics/compositor.h`：快照怎样进入合成器，预览怎样消费合成结果。最后再深入 `CanvasRenderer` 的 GL 实现。
4. 看 `src/audio/audiobuffer.h` 和 `audiomixer.h`：区分采样帧和单声道采样值。当前音频格式为 48 kHz、双声道交错 float。
5. 看 `src/output/outputtimeline.cpp`：`read()` 先收集媒体、检查生产者，再按目标时间选图像，拼出该视频帧对应的音频区间。当前输出节奏为 30 fps，因此每个视频帧对应 1600 个音频帧、3200 个 float。
6. 看 `recordingcontroller.cpp` 与 `streamingcontroller.cpp`：它们各自拥有时间线与编码器，负责停止、错误和恢复策略。最后读 `mediaencoder.cpp` 的 FFmpeg 细节。

## 哪些复杂性有必要保留

- **来源聚合**：将各控制器的媒体快照与来源管理器的可见性、变换和层级合并，渲染端无需分别查询所有控制器。
- **独立输出订阅**：录制与直播不能争抢同一个消费队列。`OutputBus` 发布到各自的有界 `OutputTap`；一个输出停止后，另一个仍可继续消费。
- **统一单调时钟**：音视频对齐依靠媒体时间戳，不能使用会被系统校时改变的墙上时间。
- **缓冲与故障策略**：视频可复用上一帧，缺失音频补静音；音频队列溢出会报错。直播恢复时可丢弃旧历史，录制还需要保证文件封装和连续性。
- **GPU 资源生命周期**：合成纹理租约、GL 上下文归属和工作线程清理涉及实际资源约束，不能因为看起来繁琐就删除。
- **销毁顺序**：主窗口先停止输出，再释放合成器与混音器；面板必须早于其引用的管理器销毁。这也是部分 QObject 延后设置 parent 的原因。

## 如何判断能不能删

`Compositor` 中标记 legacy 的输出接口仍有诊断和测试用途。“旧”不等于“无用”，删除前应查生产代码和测试调用，明确替代路径。本次保留这些接口，也保留设备错误处理、测试延迟注入。

后续优先沿一个功能闭环整理：读调用者和测试 → 明确输入输出及所属线程 → 改命名或抽取职责 → 运行对应测试。只有在确认没有调用者、替代方案已覆盖行为后，才删除接口或模块。

## 面试时应该能回答的问题

- 添加一个图片来源，哪些对象依次参与？状态和图像分别由谁持有？
- 为什么预览、录制和直播不能随意共享同一个消费队列？
- 视频帧率不稳定时，时间线如何输出固定节奏？音频空缺如何处理？
- 为什么停止按钮按下后不能立即销毁编码器和合成器？
- 直播积压与录制积压为什么需要不同的处理策略？

回答时结合上述实现与对应测试说明边界，不把尚未验证的硬件编码、设备兼容性或长时间稳定性当成已有保证。

## 视频来源模型

`VideoSource` 是抽象模型基类，直接保存名称、显隐、变换、画面尺寸、状态和错误。
`ImageSource` 直接保存文件路径，`DisplaySource` 保存鼠标采集设置，`CameraSource` 保存设备、期望模式、协商帧率与采集描述。
来源类别直接由具体类表达，不再维护类型枚举或 `type()` 接口；同一对象不会通过调整设置变成另一种来源。
通用字段和专属字段直接属于对应来源，不使用独立的 Config、Info、Status 数据类。
尺寸变化后的变换适配通过虚函数派发：通用来源仅初始化未设置的变换，摄像头还按新画幅比例调整已有高度并保留宽度。
状态和错误作为一次更新发布，摄像头恢复采集时同时更新模式并清除旧错误。

管理器通过 `unique_ptr` 拥有来源，添加操作转移所有权，外部只取得只读视图。
`source()` 与 `sources()` 返回的指针在来源移除后失效；需要在信号回调中修改来源的遍历，
先取 `sourceIds()`，再按 ID 查询。通用属性可直接从来源读取，也可通过管理器按 ID 查询。
采集与恢复仍由现有控制器和 worker 负责，摄像头 worker 持有不可变的来源值副本，与界面模型独立。
`CameraMode::applyTo()` 将原生模式写入来源的期望采集设置，V4L2 后端从该副本读取设置。渲染端继续消费 `RenderSourceSnapshot`。
术语定义见 [来源模型词汇](../CONTEXT.md)。

## 接口与生命周期约定

| 对象 | 调用线程与数据归属 | 停止与释放 |
| --- | --- | --- |
| 来源管理器 / 控制器 | 界面线程维护状态，向媒体消费者发布快照 | 控制器管理采集 worker，迟到结果用 generation 隔离 |
| 采集 worker | 设备会话属于工作线程，帧与状态通过受锁保护的接口传递 | stop 只请求停止，析构等待退出 |
| Compositor / CanvasRenderer | GL 操作属于渲染线程和对应上下文；预览持有纹理租约 | 先释放租约，再停止合成；GL 资源在所属上下文清理 |
| OutputTimeline / MediaEncoder | 每个输出线程独占实例；时间线不自行加锁 | 先停止输出，再释放合成器和混音器；录制结束须完成文件封装 |
| OutputBus / OutputTap | 总线由生产者持锁访问；每个订阅队列自行加锁 | 消费者释放订阅即可退出，不影响其他消费者 |

画布尺寸与帧率由 `graphics/videoformat.h` 定义，音频格式沿用 `AudioFormat`，
每个视频帧对应的音频帧数在 `output/outputformat.h` 推导。
录制和直播分别使用 `RecordingState`、`StreamingState`；`outputStateName()` 仅用于稳定的诊断名称。
停止请求发出后仍处于 Stopping，必须等 worker 释放资源并退出后才对外报告结束。

## 构建与测试边界

`openlive_core` 编译媒体、来源管理与输出实现，`openlive_ui` 编译窗口、面板和弹窗。
应用与测试链接同一实现；图标、样式与着色器随所属库链接到消费者，不在测试中重复维护。
图片控制器当前包含文件选择操作，因此 core 仍依赖 Qt Widgets。

`VideoSourcePanel` 负责来源列表；添加与移除弹窗位于 `sourceadddialog.cpp`，
滤镜编辑位于 `sourcefiltersdialog.cpp`。通用小控件构造放在 `sourcewidgets.h`。

主窗口测试分为离屏 UI、GPU 集成与显式硬件验收三个套件。
运行方法和格式检查见 [维护与验证](maintenance.md)。
设备能力、实现范围和实机记录见 [Linux 摄像头采集与验收](capture-validation.md)。
