# OpenLive Studio

基于 Qt6 的 Windows 桌面录制与直播工具，采用 QML + Qt Widgets 混合界面。功能按阶段接入，当前能力与未验证范围见项目文档。

## 目录结构

```text
src/
  main.cpp          应用入口
  mainwindow.h      Widgets 主窗口声明
  mainwindow.cpp    Widgets 主窗口与界面组装
  core/             场景、来源条目、选择状态与主题管理
  sources/          图片探测等来源能力
  qml/              QML 面板和对话框
assets/icons/       共用 SVG 图标及来源声明
tests/              Qt Test、CTest 配置与验证记录
docs/               项目方案、路线、架构说明和 HTML 原型
.github/workflows/  CI 配置
```

C++ 头文件与实现文件放在同一模块目录，由根 `CMakeLists.txt` 组装应用。测试继续使用独立目标，源文件和头文件搜索路径在 `tests/CMakeLists.txt` 中配置。QML 模块 URI 与资源地址不因 C++ 目录迁移而改变。后续渲染模块随 OpenGL 合成功能加入，本次不创建占位目录或拆分库目标。

## 构建与项目文档

使用包含 Qt Test、Qt Quick Widgets、Quick Controls 和 SVG 的 Qt6 开发环境，从仓库根目录配置 CMake。构建和测试命令见[测试说明](tests/README.md)。`build/` 用于本地构建产物，已忽略，不属于源码结构。

- [项目方案](docs/openlive-studio-plan.md)
- [实施路线](docs/openlive-studio-roadmap.md)
- [架构说明](docs/openlive-studio-architecture.md)
- [UI 原型](docs/openlive-studio-prototype.html)
- [协作规则](AGENTS.md)
