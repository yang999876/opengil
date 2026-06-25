# openGil

openGil 是一个面向命令行、脚本和 agent 的 `.gil` 安全编辑工具。它不把
整个文件转换成巨大 JSON 再写回，而是提供经过验证的原子操作：读取、定位、
修改局部结构，并尽量保留未知字段和未修改区域。

当前主要产物：

- `build/Release/opengil.exe`
- `build/Release/opengil.cp310-win_amd64.pyd`，Python binding，具体后缀随 Python 版本变化

## 构建

```powershell
git submodule update --init --recursive
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

本仓库使用 `third_party/pybind11` 构建 Python binding，并保留
`third_party/stb` 供图像相关能力使用。

## 快速使用

```powershell
.\build\Release\opengil.exe --version
.\build\Release\opengil.exe inspect --input .\tests\fixtures\test1.gil
.\build\Release\opengil.exe validate --input .\tests\fixtures\test1.gil
.\build\Release\opengil.exe list-prefabs --input .\tests\fixtures\test1.gil
.\build\Release\opengil.exe get-model --input .\tests\fixtures\test1.gil --prefab-id 1086324737
.\build\Release\opengil.exe set-model --input .\tests\fixtures\test1.gil --prefab-id 1086324737 --asset-id 20001220 --dry-run
```

机器可读输出走 stdout JSON；日志和诊断走 stderr。写操作建议先
`--dry-run`，确认 summary 后再写出，并在写后运行 `validate`。

`validate` 只做结构校验：检查 `.gil` envelope 尺寸和 protobuf wire
payload 解析。它不证明 id 唯一、scene/preview 镜像完整、tab 映射完整或
没有悬空引用。

## 文档

- [能力说明](docs/capabilities.zh.md)：这个工具能做什么，以及常用 CLI 示例。
- [开发说明](docs/development.zh.md)：架构边界、安全规则、如何新增能力。
- [Agent skill](skills/gil-editing/SKILL.md)：agent 调用 openGil 的工作准则。
