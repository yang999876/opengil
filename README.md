# openGil

openGil 是一个面向命令行、脚本和 agent 的 `.gil` 安全编辑工具。它提供经过验证的原子操作，用来读取、定位和修改局部结构，并尽量保留未知字段和未修改区域。本项目只解明了部分能力，并不是完整的gil解析，欢迎大家提交PR提供新能力。本项目提供了skill和cli，推荐使用ai部署和使用本项目，或者直接使用release版本。

# 警告
本项目不一定稳定，请务必对存档副本进行操作，而不是直接修改主存档，因为直接修改存档造成内容丢失，开发者概不负责。

当前主要产物：

- `build/Release/opengil.exe`
- `build/Release/opengil.cp310-win_amd64.pyd`，Python binding，具体后缀随 Python 版本变化
- `build/Release/opengil_gui.exe`，Windows 只读浏览器

## 初次部署

下面的步骤以 Windows PowerShell 为例，默认当前目录位于仓库根目录。

### 环境准备

首次构建前请先准备好这些工具：

- Git
- CMake 3.20 或更新版本
- Visual Studio 2022 / Build Tools，并安装 C++ 桌面开发工具链
- Python 3，并确保 CMake 可以找到解释器和开发头文件

### 获取源码

```powershell
git clone <your-repo-url> opengil
cd .\opengil
git submodule update --init --recursive
```

### 配置与编译

```powershell
cmake -S . -B build
cmake --build build --config Release
```

编译完成后，常用产物位于：

```text
build/Release/opengil.exe
build/Release/opengil.cp310-win_amd64.pyd
build/Release/opengil_gui.exe
```

## 第一次跑通 CLI

仓库自带 `tests/fixtures/test1.gil`，可以直接用它验证构建结果。

```powershell
.\build\Release\opengil.exe --version
.\build\Release\opengil.exe validate --input .\tests\fixtures\test1.gil
.\build\Release\opengil.exe inspect --input .\tests\fixtures\test1.gil
.\build\Release\opengil.exe list-prefabs --input .\tests\fixtures\test1.gil
.\build\Release\opengil.exe get-model --input .\tests\fixtures\test1.gil --prefab-id 1086324737
.\build\Release\opengil.exe set-model --input .\tests\fixtures\test1.gil --prefab-id 1086324737 --asset-id 20001220 --dry-run
```

命令默认输出 JSON，适合脚本、工具链和 agent 直接消费。

## 第一次在 Python 中调用

构建完成后，可以直接把 `build/Release` 加到 `sys.path` 并导入 `opengil`：

```python
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path("build/Release").resolve()))

import opengil

doc = opengil.open("tests/fixtures/test1.gil")
print(doc.validate())
print(doc.list_prefabs())

doc.set_model_asset_id(1086324737, 20001220)
doc.save("output.gil")
```

## 常用工作流

### 查看文件结构

```powershell
.\build\Release\opengil.exe inspect --input input.gil
.\build\Release\opengil.exe validate --input input.gil
.\build\Release\opengil.exe list-tabs --input input.gil
.\build\Release\opengil.exe list-prefabs --input input.gil
```

### 修改并写出新文件

```powershell
.\build\Release\opengil.exe set-model --input input.gil --output output.gil --prefab-id 1077936130 --asset-id 20001220 --dry-run
.\build\Release\opengil.exe set-model --input input.gil --output output.gil --prefab-id 1077936130 --asset-id 20001220
.\build\Release\opengil.exe validate --input output.gil
```

### 覆盖原文件

```powershell
.\build\Release\opengil.exe rename-prefab --input input.gil --in-place --prefab-id 1077936130 --name "New Name"
```

## 文档

- [能力说明](docs/capabilities.md)：当前能力总览和入口选择。
- [开发手册](docs/development.md)：如果你想新增新的功能，请阅读这里的研究规范。
- [Agent skill](skills/gil-editing/SKILL.md)：agent 调用 openGil 的工作准则。
