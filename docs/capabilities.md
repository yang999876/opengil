# openGil 能力说明

本文说明 openGil 当前能做什么，以及这些能力适合通过哪个入口使用。具体参数、返回字段和调用示例已经拆分到独立 API 文档：

- [CLI API](cli_api.md)：命令行参数、JSON 输出、退出码和完整命令参考。
- [Python API](python_api.md)：Python Binding 导入方式、`GilDocument` 方法和参数约定。
- [开发手册](development.md)：`.gil` 研究流程、项目分层、写操作规则和 PR 要求。

## 1. 项目定位

openGil 是一个面向命令行、脚本和 agent 的 `.gil` 局部安全编辑工具。它提供经过验证的原子操作，用来读取、定位和修改已经拆解清楚的结构，并尽量保留未知字段和未修改区域。

当前主要入口：

- CLI：适合一次性查询、单步写操作、CI、脚本和 agent 调用。
- Python Binding：适合把多次修改串成一个内存态工作流，最后统一保存。
- GUI：适合快速浏览 `.gil` 文件里的 prefab、scene、preview 和 nodegraph 信息。

常用构建产物：

- `build/Release/opengil.exe`
- `build/Release/opengil_gui.exe`
- `build/Release/opengil.cp310-win_amd64.pyd`

Python Binding 的 `.pyd` 文件名会随 Python 版本和 ABI 后缀变化。

## 2. 推荐工作流

对 `.gil` 文件做修改前，推荐先查、再改、再验：

```powershell
opengil inspect --input input.gil
opengil validate --input input.gil
opengil list-prefabs --input input.gil
opengil list-scene-objects --input input.gil
opengil <写操作> --input input.gil --output output.gil --dry-run
opengil <写操作> --input input.gil --output output.gil
opengil validate --input output.gil
```

需要连续执行多次写操作时，优先考虑 Python Binding：

```python
import opengil

doc = opengil.open("input.gil")
doc.rename_prefab(1077936130, "New Name")
doc.set_model_asset_id(1077936130, 20001220)
doc.save("output.gil")
```

## 3. 文件检查与研究能力

openGil 可以帮助贡献者检查 `.gil` 文件结构、验证文件能否稳定读取，并对比 before/after 样本。

已公开能力：

- 查看 `.gil` 文件头、payload 大小和 top-level field 分布。
- 执行结构校验。
- 对比两份样本，输出变化的 top-level field。

典型命令：

```powershell
opengil inspect --input input.gil
opengil validate --input input.gil
opengil diff-summary --before gil1.gil --after gil2.gil
```

详细参数见 [CLI API](cli_api.md#21-文件检查与版本)。

## 4. 查询能力

openGil 提供多组只读查询，用来定位后续写操作所需的 id 和当前语义状态。

已公开能力：

- 列出 tab 和 tab 内 prefab。
- 列出 prefab、prefab 所属 tab、模型信息。
- 列出 scene 对象和 preview 对象。
- 列出 nodegraph。
- 列出 custom variables。
- 列出 UI primitives。

典型命令：

```powershell
opengil list-tabs --input input.gil
opengil list-prefabs --input input.gil
opengil list-scene-objects --input input.gil
opengil list-preview-objects --input input.gil
opengil list-nodegraphs --input input.gil
opengil custom-vars list --input input.gil
opengil ui list --input input.gil
```

详细参数见 [CLI API](cli_api.md#22-查询命令) 和 [Python API](python_api.md#14-基础文件与查询方法)。

## 5. Prefab、模型与 Tab 能力

openGil 可以修改 prefab 名称、模型 asset id，也可以删除、克隆或复制 prefab。

已公开能力：

- 设置 prefab 模型。
- 设置空模型。
- 重命名 prefab。
- 删除 prefab。
- 克隆 prefab 到指定 tab。
- 复制 prefab 到指定 tab。

典型命令：

```powershell
opengil set-model --input input.gil --output output.gil --prefab-id 1077936130 --asset-id 20001220
opengil rename-prefab --input input.gil --output output.gil --prefab-id 1077936130 --name "New Name"
opengil clone-prefab --input input.gil --output output.gil --source-prefab-id 1077936385 --tab-id 6 --new-name "Cloned Prefab"
```

详细参数见 [CLI API](cli_api.md#23-prefab模型与-tab) 和 [Python API](python_api.md#15-对象prefab-与模型方法)。

## 6. 对象与 Transform 能力

openGil 可以创建 scene 对象、创建 prefab、创建 scene prefab 实例，并修改 scene 或 preview 对象的 transform。

已公开能力：

- 创建 scene object。
- 创建 prefab。
- 创建 scene prefab instance。
- 修改 scene transform。
- 修改 preview transform。

典型命令：

```powershell
opengil create-scene-object --input input.gil --output output.gil --asset-id 20001220 --object-id 1077938001
opengil create-prefab --input input.gil --output output.gil --asset-id 20001220 --prefab-id 1077938002
opengil set-scene-transform --input input.gil --output output.gil --object-id 1086324737 --pos-x 7 --pos-y 8 --pos-z 9
```

详细参数见 [CLI API](cli_api.md#24-对象创建与-transform) 和 [Python API](python_api.md#15-对象prefab-与模型方法)。

## 7. NodeGraph 与 Projectile 能力

openGil 可以把 nodegraph 挂载到 prefab，也可以修改 projectile motion 参数。

已公开能力：

- 挂载单个 nodegraph。
- 挂载当前文件中可用的全部 nodegraph。
- 通过 `x/y` 或 `angle/speed` 修改 projectile motion。

典型命令：

```powershell
opengil attach-nodegraph --input input.gil --output output.gil --prefab-id 1077936130 --nodegraph-id 1073741913
opengil attach-all-nodegraphs --input input.gil --output output.gil --prefab-id 1077936130
opengil set-projectile-motion --input input.gil --output output.gil --prefab-id 1077936385 --angle 80 --speed 20 --gravity 20
```

详细参数见 [CLI API](cli_api.md#25-nodegraph-与-projectile) 和 [Python API](python_api.md#17-nodegraphprojectile-与-custom-vars-方法)。

## 8. Custom Variables 能力

openGil 可以读取和修改 prefab custom variable 定义，并同步到关联对象。

已公开能力：

- 列出 custom variables。
- 添加 custom variable。
- 删除 custom variable。
- 在 prefab 之间复制全部 custom variables。
- 把源 prefab 的 custom variables 同步到同一 tab 内其他 prefab。

典型命令：

```powershell
opengil custom-vars list --input input.gil
opengil custom-vars add --input input.gil --output output.gil --prefab-id 1077936130 --name openGilVar --type str
opengil custom-vars sync-tab --input input.gil --output output.gil --source-prefab-id 1077936340 --tab-id 6
```

详细参数见 [CLI API](cli_api.md#26-custom-variables) 和 [Python API](python_api.md#17-nodegraphprojectile-与-custom-vars-方法)。

## 9. Decoration、Attachment 与 Pixel Art 能力

openGil 可以为 prefab 增加 decoration、生成 attachment point，也可以从 PNG 导入像素风结构。

已公开能力：

- 添加 decoration。
- 添加 attachment point。
- 根据 decoration 生成 attachment points。
- 从 PNG 导入 decoration prefab。

典型命令：

```powershell
opengil decoration add --input input.gil --output output.gil --prefab-id 1077936385 --asset-id 20001220 --name Deco
opengil attachment add --input input.gil --output output.gil --prefab-id 1077936385 --name Hand --display-name "Hand Point"
opengil pixel-art import-decoration --input input.gil --output output.gil --png pixel.png --prefab-id 1077939001 --asset-id 20001220 --pixel-size 0.25
```

详细参数见 [CLI API](cli_api.md#27-decorationattachment-与-pixel-art) 和 [Python API](python_api.md#18-decorationattachment-与-ui-方法)。

## 10. UI Primitive 能力

openGil 可以读取和编辑 UI primitive。

已公开能力：

- 列出 UI primitive。
- 修改 primitive 类型、颜色、transform、layer、名称。
- 删除一个或多个 primitive。
- 从 PNG 导入 UI primitive 集合。

典型命令：

```powershell
opengil ui list --input input.gil
opengil ui set-color --input input.gil --output output.gil --primitive-index 0 --color -65536
opengil ui delete --input input.gil --output output.gil --primitive-indexes 0,2,3
opengil ui import-pixel --input input.gil --output output.gil --png pixel.png --pixel-size 8
```

详细参数见 [CLI API](cli_api.md#28-ui-primitive) 和 [Python API](python_api.md#18-decorationattachment-与-ui-方法)。

## 11. GUI 能力

Windows 下可以直接打开只读 GUI：

```powershell
.\build\Release\opengil_gui.exe
```

GUI 适合快速浏览：

- prefab 列表
- scene 对象
- preview 对象
- nodegraph 列表
- 基础校验状态
