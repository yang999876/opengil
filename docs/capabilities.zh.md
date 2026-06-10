# openGil 能力说明

这份文档回答一个问题：**这个仓库现在能干什么？**

openGil 是一个面向 agent 和命令行的 `.gil` 文件安全编辑工具。它的目标不是把整个 `.gil` 文件 dump 成巨大 JSON 再写回，而是提供一组经过验证的原子能力：读取、定位、修改局部结构，然后尽量保持未知字段和未修改区域不变。

当前主产物是：

```text
build/Release/opengil.exe
```

所有机器可读输出默认走 stdout JSON。人类日志和错误诊断走 stderr。

## 总体能力

openGil 当前可以做这些事：

- 读取 `.gil` 文件基本信息
- 结构校验 `.gil` envelope 和 protobuf wire payload
- 列出 tab、prefab、nodegraph、模型引用、UI primitive
- 修改 prefab 模型 asset id
- 把 prefab 模型设置成 empty model
- 重命名、删除、克隆、复制 prefab 到 tab
- 创建 scene object、prefab、scene prefab instance
- 修改 scene / preview transform
- 绑定 nodegraph
- 修改 projectile motion
- 管理 custom variable 定义
- 添加 decoration
- 添加 attachment point
- 从 decoration 推导 attachment point
- 查看和修改 UI primitive
- 为 agent 提供 skill 使用说明

它现在适合：

- agent 自动修改 `.gil`
- 你自己用 CLI 做安全编辑
- 作为后续 `.gil` 研究能力的承载工程

它暂时不适合：

- 当作完整 `.gil` 可视化编辑器
- 当作完整 protobuf schema 浏览器
- 直接公开给完全不了解风险的用户随便改正式文件

## 基础读取能力

### 查看文件信息

```powershell
opengil inspect --input test.gil
```

返回文件大小、header 信息、payload 信息、sha256 等基础报告。

### 结构校验

```powershell
opengil validate --input test.gil
```

注意：这是结构校验，不是完整语义校验。

它会检查：

- `.gil` envelope 尺寸
- payload 是否能按 protobuf wire 格式解析

它不会完整检查：

- id 是否全局唯一
- prefab、scene、preview 是否完全同步
- tab 映射是否完整
- 是否存在悬空引用

### 文件差异摘要

```powershell
opengil diff-summary --before before.gil --after after.gil
```

用于研究 before/after 样本，帮助定位哪些 top-level field 发生变化。

## 语义列举能力

### 列出 tabs

```powershell
opengil list-tabs --input test.gil
```

### 列出 prefabs

```powershell
opengil list-prefabs --input test.gil
opengil list-prefabs --input test.gil --tab TabName
```

### 查询 prefab 所在 tabs

```powershell
opengil list-prefab-tabs --input test.gil --prefab-id 1077936130
```

### 查询 prefab 模型

```powershell
opengil get-model --input test.gil --prefab-id 1077936130
```

会返回 prefab 自身、scene 引用、preview 引用中的模型 asset id。

### 列出 nodegraphs

```powershell
opengil list-nodegraphs --input test.gil
```

会返回已识别 nodegraph 的 id、name、path、role 和一些计数信息。

## 模型修改能力

### 设置 prefab 模型

```powershell
opengil set-model --input input.gil --output output.gil --prefab-id 1077936130 --asset-id 20001220
```

会联动修改：

- prefab 定义中的模型 asset id
- scene 中引用该 prefab 的模型 asset id
- preview 中引用该 prefab 的模型 asset id，如果存在

### 设置 empty model

```powershell
opengil set-empty-model --input input.gil --output output.gil --prefab-id 1077936130
```

用于把 prefab 切成已验证的 empty model 结构。

## Prefab 操作能力

### 重命名 prefab

```powershell
opengil rename-prefab --input input.gil --output output.gil --prefab-id 1077936130 --name "New Name"
```

主要修改 prefab 定义里的名字字段。

### 删除 prefab

```powershell
opengil delete-prefab --input input.gil --output output.gil --prefab-id 1077936130
```

当前会处理：

- 删除 `top4` 中的 prefab entry
- 清理 `top6` 里的相关映射
- 删除 prefab 拥有的 `top27.field1` decoration
- 修剪直接引用该 prefab 或 decoration id 的 `top10` 记录

注意：当前不会默认删除 `top8`。

### 克隆 prefab 到 tab

```powershell
opengil clone-prefab --input input.gil --output output.gil --source-prefab-id 1077936385 --tab-id 6 --new-name "Cloned Prefab"
```

会处理：

- 克隆 `top4` prefab 定义
- 添加 `top6` tab 映射
- 添加未分类 prefab 映射
- 偏移 preview 位置
- 如果源 prefab 有 prefab-side decoration，会克隆对应 `top27.field1`

### 复制 prefab 到 tab

```powershell
opengil copy-prefab-to-tab --input input.gil --output output.gil --source-prefab-id 1077936385 --tab-id 6
```

和 clone 类似，但名字可以不传。默认名字是源 prefab 名字加 `-copy`。

Windows shell 下遇到非 ASCII tab 名时，优先使用 `--tab-id`，不要依赖 `--tab`。

## 对象和 Transform 能力

### 创建 scene object

```powershell
opengil create-scene-object --input input.gil --output output.gil --asset-id 20001220 --object-id 1077938001
```

会复制已观察到的 scene object 模板， patch object id、asset id、transform，并添加 scene 映射。

### 创建 prefab

```powershell
opengil create-prefab --input input.gil --output output.gil --asset-id 20001220 --prefab-id 1077938002
opengil create-prefab --input input.gil --output output.gil --asset-id 20001220 --prefab-id 1077938002 --template template.gil
```

如果当前文件没有合适模板，可以传 `--template`。

### 创建 scene prefab instance

```powershell
opengil create-scene-prefab-instance --input input.gil --output output.gil --prefab-id 1077938002 --asset-id 20001220 --object-id 1077938003
```

用于创建引用 prefab 的 scene object。

### 修改 scene transform

```powershell
opengil set-scene-transform --input input.gil --output output.gil --object-id 1086324737 --pos-x 7 --pos-y 8 --pos-z 9
```

### 修改 preview transform

```powershell
opengil set-preview-transform --input input.gil --output output.gil --object-id 1077936362 --pos-x 1 --pos-y 2 --pos-z 3
```

注意：transform 写入会替换完整 transform message。未传的 position/rotation 默认 `0`，未传的 scale 默认 `1`。如果想保留某个轴，调用方应该显式传入原值。

## NodeGraph 能力

### 绑定单个 nodegraph

```powershell
opengil attach-nodegraph --input input.gil --output output.gil --prefab-id 1077936130 --nodegraph-id 1073741913
```

会把已验证的 nodegraph reference 挂到：

- prefab
- scene
- preview

### 绑定所有已发现 nodegraph

```powershell
opengil attach-all-nodegraphs --input input.gil --output output.gil --prefab-id 1077936130
```

用于把文件中可识别的 nodegraph definition 全部挂到目标 prefab。

## Projectile Motion 能力

### 按角度和速度设置

```powershell
opengil set-projectile-motion --input input.gil --output output.gil --prefab-id 1077936385 --angle 80 --speed 20 --gravity 20
```

### 直接设置 x/y 速度

```powershell
opengil set-projectile-motion --input input.gil --output output.gil --prefab-id 1077936385 --x 3.47 --y 19.70 --gravity 20
```

当前能力针对 prefab-space component。识别逻辑基于已验证的 projectile motion component 结构。

## Custom Variables 能力

### 列出 custom variables

```powershell
opengil custom-vars list --input input.gil
opengil custom-vars list --input input.gil --prefab-id 1077936130
```

### 添加变量定义

```powershell
opengil custom-vars add --input input.gil --output output.gil --prefab-id 1077936130 --name openGilVar --type str
```

### 删除变量定义

```powershell
opengil custom-vars remove --input input.gil --output output.gil --prefab-id 1077936130 --name openGilVar
```

### 复制全部变量定义

```powershell
opengil custom-vars copy-all --input input.gil --output output.gil --from-prefab-id 1077936130 --to-prefab-id 1077936131
```

### 同步整个 tab

```powershell
opengil custom-vars sync-tab --input input.gil --output output.gil --source-prefab-id 1077936340 --tab-id 6
```

支持的类型：

```text
entity
int
bool
float
str / string
vec / vec3
```

限制：当前只写 custom variable 定义，不写运行时值。

## Decoration 能力

### 添加 decoration

```powershell
opengil decoration add --input input.gil --output output.gil --prefab-id 1077936385 --asset-id 20001220 --name Deco --pos-y 1.9 --scale-x 0.3 --scale-y 0.04 --scale-z 0.3
```

会处理：

- 添加 prefab-side `top27.field1`
- 为匹配的 preview/scene entry 添加 scene mirror `top27.field2`
- 更新 prefab reference list
- 更新 scene reference list

## Attachment 能力

### 添加 attachment point

```powershell
opengil attachment add --input input.gil --output output.gil --prefab-id 1077936385 --name Hand --display-name "Hand Point" --pos-x 0.48 --pos-y 1.52 --rot-x -37.9 --rot-y 81.9
```

可选传 `--object-id` 来只更新一个 scene entry。不传则更新所有引用该 prefab 的 scene entry。

### 从 decoration 推导 attachment

当前 C++ API 已有 `add_attachment_points_from_decorations`，测试覆盖了从 decoration 推导手/头 attachment 的流程。CLI 目前主入口是 `attachment add`。

## UI Primitive 能力

### 列出 UI primitives

```powershell
opengil ui list --input input.gil
opengil ui list --input input.gil --controller-entry-id 1073741855
```

### 追加 UI primitive

```powershell
opengil ui append --input input.gil --output output.gil --template template.gil --target-controller-entry-id 1073741855 --template-primitive-index 0
```

### 批量追加 UI primitive

```powershell
opengil ui append-many --input input.gil --output output.gil --template template.gil --target-controller-entry-id 1073741855 --template-primitive-index 0 --count 3
```

### 保留指定 primitive

```powershell
opengil ui retain --input input.gil --output output.gil --controller-entry-id 1073741855 --primitive-indexes 0,2,3
```

### 修改 primitive 属性

```powershell
opengil ui set-type --input input.gil --output output.gil --controller-entry-id 1073741855 --primitive-index 0 --type-id 100001
opengil ui set-color --input input.gil --output output.gil --controller-entry-id 1073741855 --primitive-index 0 --color -65536
opengil ui set-transform --input input.gil --output output.gil --controller-entry-id 1073741855 --primitive-index 0 --pos-x 10 --pos-y 20 --width 80 --height 80 --scale-x 1 --scale-y 1 --scale-z 1 --rot-z 0
opengil ui set-layer --input input.gil --output output.gil --controller-entry-id 1073741855 --primitive-index 0 --layer 9
opengil ui set-name --input input.gil --output output.gil --controller-entry-id 1073741855 --primitive-index 0 --name ButtonA
```

### 从模板复制 transform

```powershell
opengil ui copy-transform-from-template --input input.gil --output output.gil --template template.gil --controller-entry-id 1073741855 --primitive-index 0 --template-primitive-index 1
```

限制：`ui import-geometrize` 和 `ui import-pixel` 当前没有实现。

## 未来批处理方向

CLI 现在聚焦单次原子操作。未来需要批量修改时，优先方向不是恢复
CLI 批处理入口，而是通过 Python `.pyd` / Python binding 暴露
内存态 document API：一次读取 `.gil`，在同一个 document 上连续应用多个
operation，最后统一 dry-run 或写出。

这样可以保留当前 CLI JSON 兼容层的简单性，同时让 agent 和脚本在 Python
层组合复杂流程。

## 安全和输出能力

所有写命令都应该优先使用：

```powershell
--dry-run
```

写出文件时推荐显式传：

```powershell
--output output.gil
```

覆盖原文件必须显式使用：

```powershell
--in-place
```

CLI 输出是标准 JSON envelope，形状大致是：

```json
{
  "schemaVersion": 1,
  "toolVersion": "0.1.0",
  "ok": true,
  "command": "set-model",
  "input": {
    "path": "test.gil",
    "sha256": "..."
  },
  "output": {
    "path": "out.gil",
    "sha256": "..."
  },
  "result": {},
  "warnings": [],
  "errors": []
}
```

## Skill 能力

仓库内置 agent skill：

```text
skills/gil-editing/
```

安装脚本：

```powershell
.\scripts\install-skill.ps1
```

skill 会指导 agent：

- 优先调用 `opengil`
- 不默认 dump 巨大 JSON IR
- 复杂写操作先 dry-run
- 多次修改暂时逐条 dry-run / 写入；未来使用 Python binding 的内存态 document API 批处理
- 写后 validate
- 未知结构走 before/after diff

## 当前明确不能做什么

当前不做或不保证：

- 不保证完整语义 validate
- 不写 custom variable 运行时值
- 不实现 `ui import-geometrize`
- 不实现 `ui import-pixel`
- 不提供完整 GUI
- 不保证 protobuf generated class 的无损写回
- 不把 `.gil` 整体转换成巨大 JSON IR 再写回
- 不保证未验证结构可以随便猜着改

## 推荐定位

一句话总结：

```text
openGil 现在是一个面向 agent 的 .gil 安全原子编辑器。
```

它的强项不是“知道全部协议”，而是：

- 对已验证结构提供稳定命令
- 尽量局部修改
- 保留未知字段
- 给 agent 提供可靠、可组合、可逐步验证的工具接口
