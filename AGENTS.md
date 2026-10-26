# OpenLive Studio 项目协作规则

## Git 提交

- 实现完成后不自动暂存、提交或推送，保留工作区改动供用户审核。
- 用户审核通过并明确要求提交后，只提交当前授权范围内的文件；用户直接要求提交既有改动时，按其明确范围处理。
- 提交前检查工作区和暂存区，只暂存相关文件，不夹带用户的其他改动或其他步骤的未审核内容。
- 提交信息使用英文，不包含步骤编号或任务编号，采用简洁的 `type(scope): description` 或 `type: description` 形式。
- 提交与推送分别授权；未经明确要求不推送，不擅自修改历史或覆盖已有提交。
- 提交完成后报告 commit ID、提交信息及是否仍有未提交改动。

示例：

```text
feat(ui): add scene dock
fix(output): keep recording active when streaming stops
docs: align project scope with UI prototype
```

## 图标资源

- 图标按需下载，具体功能用到时再引入对应文件，不提前下载整套图标。
- 界面操作图标优先统一使用 [Lucide](https://lucide.dev/icons) 的 SVG。操作图标集中放在 `assets/icons/`。
