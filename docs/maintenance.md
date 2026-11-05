# 维护与验证

## 小步修改

先沿一条功能路径确认调用者、所属线程和数据所有权，再修改职责、命名或类型。
行为重构与纯格式调整分别审核。不要合并录制和直播的故障策略，也不要移除独立输出队列或 GL 资源释放约束。

## 格式

使用 clang-format **22.1.8** 和仓库 `.clang-format`。
可以用 `python3 -m pip install clang-format==22.1.8` 安装；已有同版本工具时直接复用。
脚本默认比较 HEAD 与工作区，检查新增和修改的 C++ 行；新文件检查全文。
它不会要求一次格式化所有历史代码。CI 使用相同版本，PR 以共同祖先作为比较基准。

```bash
python3 scripts/format-changes.py
python3 scripts/format-changes.py --fix
# 工具不在 PATH 时：
CLANG_FORMAT=/path/to/clang-format python3 scripts/format-changes.py
```

`--fix` 包含当前工作区所有改动与未跟踪的 C++ 文件，使用前注意自己的其他未提交修改。
指定 `--base <revision>` 可检查相对其他版本的变化。

## 测试

先按 README 配置 Qt / FFmpeg 环境并完成构建，再运行相关套件。

```bash
ctest --test-dir build/linux-ffmpeg71 -L smoke --output-on-failure
ctest --test-dir build/linux-ffmpeg71 -R '^ui.mainwindow.gpu$' --output-on-failure
ctest --test-dir build/linux-ffmpeg71 --output-on-failure
```

- `ui.mainwindow`：强制 offscreen，检查界面结构、来源操作、配置与布局。
- `ui.mainwindow.gpu`：需要桌面 OpenGL，检查预览、编辑、错误显示和输出停止流程。
- `ui.mainwindow.hardware`：摄像头与长时间录制验收，保留环境变量显式启用。

```bash
OPENLIVE_CAMERA_HARDWARE=1 ctest --test-dir build/linux-ffmpeg71 -R '^ui.mainwindow.hardware$' --output-on-failure
OPENLIVE_RECORDING_SOAK_SECONDS=30 build/linux-ffmpeg71/tests/tst_mainwindow_hardware recordingAcceptance
```

硬件套件默认跳过需要设备的用例；整体测试通过不能代替这些验收。
GPU 套件应在可创建 OpenGL 上下文的桌面或 CI 的 Xvfb 环境执行。
