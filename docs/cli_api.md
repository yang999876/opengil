# openGil CLI API

本文从能力说明拆分而来，覆盖 CLI 通用约定、命令参数、示例和返回重点。

## 1. CLI 通用约定

### 1.1 输入、输出与 JSON envelope

CLI 默认把结果输出为 JSON，适合脚本、工具链和 agent 直接消费。成功和失败都走
`stdout`，结构统一。

常见 envelope 字段：

- `schemaVersion`：CLI 输出结构版本，当前为 `1`
- `toolVersion`：工具版本
- `ok`：本次命令是否成功
- `command`：实际执行的命令名
- `input`：输入文件信息，包含 `path` 和 `sha256`
- `output`：输出文件信息；只读命令或 `--dry-run` 时为 `null`
- `result`：命令主体结果
- `warnings`：警告数组
- `errors`：错误数组；失败时包含 `code` 和 `message`

示例：

```json
{
  "schemaVersion": 1,
  "toolVersion": "0.1.0",
  "ok": true,
  "command": "inspect",
  "input": {
    "path": "input.gil",
    "sha256": "..."
  },
  "output": null,
  "result": {},
  "warnings": [],
  "errors": []
}
```

### 1.2 只读命令与写命令

只读命令通常只需要 `--input`，写命令统一遵循下面的输出规则：

- 推荐显式传 `--output output.gil`
- 覆盖原文件时传 `--in-place`
- 预演一次修改但不落盘时传 `--dry-run`
- 需要把完整 JSON envelope 额外保存到文件时传 `--report report.json`

写命令的工作方式：

- 正常写出时，CLI 先生成新字节流，再写入临时文件
- 临时文件会先经过一次 `validate`
- 校验通过后，再原子替换目标文件
- `--in-place` 会在原文件旁生成一个 `*.bak` 备份
- `--dry-run` 不写文件，`result` 中会附加 `dryRun: true`

### 1.3 常用工作流

推荐先查、再改、再验：

```powershell
opengil inspect --input input.gil
opengil validate --input input.gil
opengil list-prefabs --input input.gil
opengil list-scene-objects --input input.gil
opengil <写操作> --input input.gil --output output.gil --dry-run
opengil <写操作> --input input.gil --output output.gil
opengil validate --input output.gil
```

查找 id 的常见入口：

- `list-tabs`：查 `tab-id`
- `list-prefabs`：查 `prefab-id`
- `list-scene-objects` / `list-preview-objects`：查 `object-id`
- `ui list`：查 `primitive-index`、`controller-entry-id`
- `list-nodegraphs`：查 `nodegraph-id`

### 1.4 Transform 参数约定

3D transform 系列命令共用下面这些参数：

- `--pos-x --pos-y --pos-z`
- `--rot-x --rot-y --rot-z`
- `--scale-x --scale-y --scale-z`

UI transform 使用另一套参数：

- `--pos-x --pos-y`
- `--width --height`
- `--scale-x --scale-y --scale-z`
- `--rot-z`

如果你是在已有对象上做局部调参，推荐先通过查询命令把当前 transform 读出来，再把希望保留的值一并传回写命令。

### 1.5 退出码

- `0`：成功
- `1`：命令行参数错误
- `2`：解析或未处理异常
- `3`：语义错误，例如目标 prefab 不存在
- `4`：校验失败
- `5`：写文件失败

### 1.6 命令索引

文件检查与版本：

- `version` / `--version`
- `inspect`
- `validate`
- `diff-summary`

查询命令：

- `list-tabs`
- `list-prefabs`
- `list-prefab-tabs`
- `get-model`
- `list-scene-objects`
- `list-preview-objects`
- `list-nodegraphs`
- `custom-vars list`
- `ui list`

Prefab / 模型 / Tab：

- `set-model`
- `set-empty-model`
- `rename-prefab`
- `delete-prefab`
- `clone-prefab`
- `copy-prefab-to-tab`

对象与 transform：

- `create-scene-object`
- `create-prefab`
- `create-scene-prefab-instance`
- `set-scene-transform`
- `set-preview-transform`
- `set-scene-object-color`

NodeGraph 与 Projectile：

- `attach-nodegraph`
- `attach-all-nodegraphs`
- `set-projectile-motion`

Custom Variables：

- `custom-vars add`
- `custom-vars remove`
- `custom-vars copy-all`
- `custom-vars sync-tab`

Decoration / Attachment / Pixel Art：

- `decoration add`
- `attachment add`
- `attachment from-decoration`
- `pixel-art import-decoration`

UI：

- `ui set-type`
- `ui set-color`
- `ui set-transform`
- `ui set-layer`
- `ui set-name`
- `ui delete`
- `ui import-pixel`

## 2. CLI 参考

## 2.1 文件检查与版本

### `version` / `--version`

用途：输出当前 CLI 版本。

示例：

```powershell
opengil version
opengil --version
```

返回重点：

- `result.version`

### `inspect`

用途：查看 `.gil` 文件头、文件大小、payload 大小和 top-level field 分布。

参数：

- 必填：`--input <path>`

示例：

```powershell
opengil inspect --input input.gil
```

返回重点：

- `result.header`
- `result.fileSize`
- `result.payloadSize`
- `result.topLevelFields[]`

### `validate`

用途：执行结构校验，确认文件可以被当前解析器稳定读取。

参数：

- 必填：`--input <path>`

示例：

```powershell
opengil validate --input input.gil
```

返回重点：

- `result.validationKind`
- `result.valid`
- `result.errors`
- `result.warnings`

### `diff-summary`

用途：对比两份 `.gil` 样本，快速定位发生变化的 top-level field。

参数：

- 必填：`--before <path>`
- 必填：`--after <path>`
- 可选：`--report <path>`

示例：

```powershell
opengil diff-summary --before gil1.gil --after gil2.gil
```

返回重点：

- `result.beforeSha256`
- `result.afterSha256`
- `result.changedTopFieldCount`
- `result.changedTopFields`

## 2.2 查询命令

### `list-tabs`

用途：列出文件中的 tab 及其包含的 prefab id。

参数：

- 必填：`--input <path>`
- 可选：`--report <path>`

示例：

```powershell
opengil list-tabs --input input.gil
```

返回重点：

- `result.count`
- `result.items[].id`
- `result.items[].name`
- `result.items[].prefabIds`

### `list-prefabs`

用途：列出 prefab；可以按 tab 名过滤。

参数：

- 必填：`--input <path>`
- 可选：`--tab <tab-name>`
- 可选：`--report <path>`

示例：

```powershell
opengil list-prefabs --input input.gil
opengil list-prefabs --input input.gil --tab Default
```

返回重点：

- `result.count`
- `result.items[].prefabId`
- `result.items[].name`
- `result.items[].modelAssetId`

### `list-prefab-tabs`

用途：查看某个 prefab 当前属于哪些 tab。

参数：

- 必填：`--input <path>`
- 必填：`--prefab-id <u64>`
- 可选：`--report <path>`

示例：

```powershell
opengil list-prefab-tabs --input input.gil --prefab-id 1077936130
```

返回重点：

- `result.count`
- `result.items[].id`
- `result.items[].name`

### `get-model`

用途：查看某个 prefab 的模型信息，包括 prefab 本体、scene 实例和 preview 实例上的模型 id。

参数：

- 必填：`--input <path>`
- 必填：`--prefab-id <u64>`
- 可选：`--report <path>`

示例：

```powershell
opengil get-model --input input.gil --prefab-id 1077936130
```

返回重点：

- `result.prefabId`
- `result.name`
- `result.prefabModelAssetId`
- `result.sceneModelAssetIds`
- `result.previewModelAssetIds`

### `list-scene-objects`

用途：列出 scene 区域对象。

参数：

- 必填：`--input <path>`
- 可选：`--report <path>`

示例：

```powershell
opengil list-scene-objects --input input.gil
```

返回重点：

- `result.count`
- `result.items[].index`
- `result.items[].objectId`
- `result.items[].refId`
- `result.items[].assetId`
- `result.items[].color`
- `result.items[].rawColor`
- `result.items[].rgbColor`
- `result.items[].colorEnabled`
- `result.items[].transform`

### `list-preview-objects`

用途：列出 preview 区域对象。

参数：

- 必填：`--input <path>`
- 可选：`--report <path>`

示例：

```powershell
opengil list-preview-objects --input input.gil
```

返回重点：

- `result.count`
- `result.items[].objectId`
- `result.items[].prefabName`
- `result.items[].assetId`
- `result.items[].color`
- `result.items[].rawColor`
- `result.items[].rgbColor`
- `result.items[].colorEnabled`
- `result.items[].transform`

### `list-nodegraphs`

用途：列出文件中的 nodegraph 资源及统计信息。

参数：

- 必填：`--input <path>`
- 可选：`--report <path>`

示例：

```powershell
opengil list-nodegraphs --input input.gil
```

返回重点：

- `result.count`
- `result.items[].id`
- `result.items[].name`
- `result.items[].path`
- `result.items[].role`
- `result.items[].nodeCount`

## 2.3 Prefab、模型与 Tab

### `set-model`

用途：把指定 prefab 的模型 asset id 同步写入 prefab、scene 和 preview 对应位置。

参数：

- 必填：`--input <path>`
- 必填：`--prefab-id <u64>`
- 必填：`--asset-id <u64>`
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil set-model --input input.gil --output output.gil --prefab-id 1077936130 --asset-id 20001220
```

返回重点：

- `result.prefabId`
- `result.prefabName`
- `result.modelAssetId`
- `result.prefabUpdated`
- `result.sceneUpdated`
- `result.previewUpdated`
- `result.changedTopFields`

### `set-empty-model`

用途：把指定 prefab 改为空模型。

参数：

- 必填：`--input <path>`
- 必填：`--prefab-id <u64>`
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil set-empty-model --input input.gil --output output.gil --prefab-id 1077936130
```

返回重点：

- `result.prefabId`
- `result.modelAssetId`
- `result.changedTopFields`

### `rename-prefab`

用途：修改 prefab 名称。

参数：

- 必填：`--input <path>`
- 必填：`--prefab-id <u64>`
- 必填：`--name <string>`
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil rename-prefab --input input.gil --output output.gil --prefab-id 1077936130 --name "New Name"
```

返回重点：

- `result.prefabId`
- `result.beforeName`
- `result.afterName`
- `result.changedTopFields`

### `delete-prefab`

用途：删除指定 prefab。

参数：

- 必填：`--input <path>`
- 必填：`--prefab-id <u64>`
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil delete-prefab --input input.gil --output output.gil --prefab-id 1077936130
```

返回重点：

- `result.kind`
- `result.prefabId`
- `result.removedDecorationIds`
- `result.changedTopFields`

### `clone-prefab`

用途：克隆一个 prefab 到目标 tab，并为新 prefab 指定名称。

参数：

- 必填：`--input <path>`
- 必填：`--source-prefab-id <u64>`
- 必填：`--new-name <string>`，也可以写成 `--name <string>`
- 目标 tab 二选一：
  - `--tab-id <u64>`
  - `--tab <tab-name>`
- 可选：`--new-prefab-id <u64>`
- 可选：`--prefab-id-start-after <u64>`
- 可选：`--preview-x-step <number>`
- 可选：`--preview-z-step <number>`
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil clone-prefab --input input.gil --output output.gil --source-prefab-id 1077936385 --tab-id 6 --new-name "Cloned Prefab"
opengil clone-prefab --input input.gil --output output.gil --source-prefab-id 1077936385 --tab Default --new-name "Cloned Prefab"
```

返回重点：

- `result.kind`
- `result.sourcePrefabId`
- `result.sourceName`
- `result.newPrefabId`
- `result.newPrefabName`
- `result.targetTab`
- `result.clonedDecorationCount`
- `result.previewPos`
- `result.changedTopFields`

Windows shell 下如果 tab 名包含非 ASCII 字符，优先使用 `--tab-id`。

### `copy-prefab-to-tab`

用途：把 prefab 复制到目标 tab；可以保留原名，也可以顺手改名。

参数：

- 必填：`--input <path>`
- 必填：`--source-prefab-id <u64>`
- 目标 tab 二选一：
  - `--tab-id <u64>`
  - `--tab <tab-name>`
- 可选：`--name <string>`
- 可选：`--new-prefab-id <u64>`
- 可选：`--prefab-id-start-after <u64>`
- 可选：`--preview-x-step <number>`
- 可选：`--preview-z-step <number>`
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil copy-prefab-to-tab --input input.gil --output output.gil --source-prefab-id 1077936385 --tab-id 6
opengil copy-prefab-to-tab --input input.gil --output output.gil --source-prefab-id 1077936385 --tab-id 6 --name "Copied Prefab"
```

返回重点：

- `result.kind`
- `result.sourcePrefabId`
- `result.newPrefabId`
- `result.newPrefabName`
- `result.targetTab`
- `result.changedTopFields`

## 2.4 对象创建与 Transform

### `create-scene-object`

用途：在 scene 区域创建一个普通对象。

参数：

- 必填：`--input <path>`
- 必填：`--asset-id <u64>`
- 可选：`--object-id <u64>`
- 可选：3D transform 参数
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil create-scene-object --input input.gil --output output.gil --asset-id 20001220 --object-id 1077938001 --pos-x 1 --pos-y 2 --pos-z 3
```

返回重点：

- `result.kind`
- `result.objectId`
- `result.assetId`
- `result.transform`
- `result.changedTopFields`

### `create-prefab`

用途：创建一个新的 prefab。

参数：

- 必填：`--input <path>`
- 必填：`--asset-id <u64>`
- 可选：`--prefab-id <u64>`
- 可选：3D transform 参数
- 可选：`--template <path>`
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil create-prefab --input input.gil --output output.gil --asset-id 20001220 --prefab-id 1077938002
opengil create-prefab --input input.gil --output output.gil --asset-id 20001220 --prefab-id 1077938002 --template template.gil
```

返回重点：

- `result.kind`
- `result.prefabId`
- `result.assetId`
- `result.transform`
- `result.changedTopFields`

### `create-scene-prefab-instance`

用途：在 scene 区域创建一个 prefab 实例对象。

参数：

- 必填：`--input <path>`
- 必填：`--prefab-id <u64>`
- 必填：`--asset-id <u64>`
- 可选：`--object-id <u64>`
- 可选：3D transform 参数
- 可选：`--template <path>`
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil create-scene-prefab-instance --input input.gil --output output.gil --prefab-id 1077938002 --asset-id 20001220 --object-id 1077938003
opengil create-scene-prefab-instance --input input.gil --output output.gil --prefab-id 1077938002 --asset-id 20001220 --template template.gil
```

返回重点：

- `result.kind`
- `result.objectId`
- `result.prefabId`
- `result.assetId`
- `result.transform`
- `result.changedTopFields`

### `set-scene-transform`

用途：修改 scene 对象的 transform。

参数：

- 必填：`--input <path>`
- 必填：`--object-id <u64>`
- 可选：3D transform 参数
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil set-scene-transform --input input.gil --output output.gil --object-id 1086324737 --pos-x 7 --pos-y 8 --pos-z 9 --rot-y 45 --scale-x 1 --scale-y 1 --scale-z 1
```

返回重点：

- `result.kind`
- `result.objectId`
- `result.transform`
- `result.changedTopFields`

### `set-preview-transform`

用途：修改 preview 对象的 transform。

参数：

- 必填：`--input <path>`
- 必填：`--object-id <u64>`
- 可选：3D transform 参数
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil set-preview-transform --input input.gil --output output.gil --object-id 1077936362 --pos-x 1 --pos-y 2 --pos-z 3
```

返回重点：

- `result.kind`
- `result.objectId`
- `result.transform`
- `result.changedTopFields`

### `set-scene-object-color`

用途：修改 scene 对象上可着色模型的颜色，并启用颜色组件。

参数：

- 必填：`--input <path>`
- 必填：`--object-id <u64>`
- 必填：`--color <i64>`，signed ARGB，例如红色 `-65536`、蓝色 `-16776961`
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil set-scene-object-color --input input.gil --output output.gil --object-id 1077936131 --color -16776961
```

返回重点：

- `result.kind`
- `result.objectId`
- `result.before.color`
- `result.before.enabled`
- `result.after.color`
- `result.after.enabled`
- `result.changedTopFields`

## 2.5 NodeGraph 与 Projectile

### `attach-nodegraph`

用途：把一个 nodegraph 挂到指定 prefab，并同步到对应 scene / preview 实例。

参数：

- 必填：`--input <path>`
- 必填：`--prefab-id <u64>`
- 必填：`--nodegraph-id <u64>`
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil attach-nodegraph --input input.gil --output output.gil --prefab-id 1077936130 --nodegraph-id 1073741913
```

返回重点：

- `result.prefabId`
- `result.nodegraphId`
- `result.nodegraphName`
- `result.prefabUpdated`
- `result.alreadyAttached`
- `result.sceneUpdated`
- `result.previewUpdated`
- `result.changedTopFields`

### `attach-all-nodegraphs`

用途：把当前文件里可挂载的 nodegraph 全部挂到指定 prefab。

参数：

- 必填：`--input <path>`
- 必填：`--prefab-id <u64>`
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil attach-all-nodegraphs --input input.gil --output output.gil --prefab-id 1077936130
```

返回重点：

- `result.prefabId`
- `result.availableCount`
- `result.attachedCount`
- `result.attachedNodegraphIds`
- `result.items[]`
- `result.changedTopFields`

### `set-projectile-motion`

用途：修改 prefab projectile motion 参数。

参数：

- 必填：`--input <path>`
- 必填：`--prefab-id <u64>`
- 输入形式二选一：
  - `--x <number>` 和 `--y <number>`
  - `--angle <number>` 和 `--speed <number>`
- 角度参数也支持别名：`--angle-deg <number>`
- 可选：`--gravity <number>`
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil set-projectile-motion --input input.gil --output output.gil --prefab-id 1077936385 --angle 80 --speed 20 --gravity 20
opengil set-projectile-motion --input input.gil --output output.gil --prefab-id 1077936385 --x 3.47 --y 19.70 --gravity 20
```

返回重点：

- `result.prefabId`
- `result.prefabName`
- `result.before`
- `result.after`
- `result.changedTopFields`

## 2.6 Custom Variables

支持的类型名：

- `entity`
- `int`
- `bool`
- `float`
- `str`
- `string`
- `vec`
- `vec3`

### `custom-vars list`

用途：列出一个或全部 prefab 的 custom variable 定义。

参数：

- 必填：`--input <path>`
- 可选：`--prefab-id <u64>`
- 可选：`--report <path>`

示例：

```powershell
opengil custom-vars list --input input.gil
opengil custom-vars list --input input.gil --prefab-id 1077936130
```

返回重点：

- `result.count`
- `result.items[].prefabId`
- `result.items[].prefabName`
- `result.items[].variables[]`

### `custom-vars add`

用途：给指定 prefab 增加一个 custom variable 定义。

参数：

- 必填：`--input <path>`
- 必填：`--prefab-id <u64>`
- 必填：`--name <string>`
- 必填：`--type <string>`
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil custom-vars add --input input.gil --output output.gil --prefab-id 1077936130 --name openGilVar --type str
```

返回重点：

- `result.kind`
- `result.prefabId`
- `result.prefabName`
- `result.variable`
- `result.synchronized`
- `result.changedTopFields`

### `custom-vars remove`

用途：删除指定 prefab 上的一个 custom variable 定义。

参数：

- 必填：`--input <path>`
- 必填：`--prefab-id <u64>`
- 必填：`--name <string>`
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil custom-vars remove --input input.gil --output output.gil --prefab-id 1077936130 --name openGilVar
```

返回重点：

- `result.kind`
- `result.prefabId`
- `result.variable`
- `result.synchronized`
- `result.changedTopFields`

### `custom-vars copy-all`

用途：把一个 prefab 的全部 custom variable 定义复制到另一个 prefab。

参数：

- 必填：`--input <path>`
- 必填：`--from-prefab-id <u64>`
- 必填：`--to-prefab-id <u64>`
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil custom-vars copy-all --input input.gil --output output.gil --from-prefab-id 1077936130 --to-prefab-id 1077936131
```

返回重点：

- `result.kind`
- `result.sourcePrefabId`
- `result.targetPrefabId`
- `result.sourceVariableCount`
- `result.synchronized`
- `result.changedTopFields`

### `custom-vars sync-tab`

用途：把源 prefab 的 custom variable 定义同步到同一 tab 内的其他 prefab。

参数：

- 必填：`--input <path>`
- 必填：`--source-prefab-id <u64>`
- 目标 tab 二选一：
  - `--tab-id <u64>`
  - `--tab <tab-name>`
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil custom-vars sync-tab --input input.gil --output output.gil --source-prefab-id 1077936340 --tab-id 6
opengil custom-vars sync-tab --input input.gil --output output.gil --source-prefab-id 1077936340 --tab Default
```

返回重点：

- `result.kind`
- `result.sourcePrefabId`
- `result.tab`
- `result.targetCount`
- `result.items[]`
- `result.changedTopFields`

## 2.7 Decoration、Attachment 与 Pixel Art

### `decoration add`

用途：给指定 prefab 增加一个 decoration，并同步生成对应 scene decoration。

参数：

- 必填：`--input <path>`
- 必填：`--prefab-id <u64>`
- 必填：`--asset-id <u64>`
- 必填：`--name <string>`
- 可选：`--color <i64>`，signed ARGB；传入后写入可着色模型颜色组件
- 可选：3D transform 参数
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil decoration add --input input.gil --output output.gil --prefab-id 1077936385 --asset-id 20001220 --name Deco --color -65536 --pos-y 1.9 --scale-x 0.3 --scale-y 0.04 --scale-z 0.3
```

返回重点：

- `result.kind`
- `result.prefabId`
- `result.sceneInstanceCount`
- `result.addedPrefabDecorations`
- `result.addedSceneDecorations`
- `result.prefabDecorationIds`
- `result.sceneDecorationIds`
- `result.changedTopFields`

### `attachment add`

用途：给指定 prefab 增加 attachment point。

参数：

- 必填：`--input <path>`
- 必填：`--prefab-id <u64>`
- 必填：`--name <string>`
- 必填：`--display-name <string>`
- 可选：`--object-id <u64>`，用于把修改限制到一个 scene 实例
- Attachment 参数字段：
  - `--pos-x`
  - `--pos-y`
  - `--rot-x`
  - `--rot-y`
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil attachment add --input input.gil --output output.gil --prefab-id 1077936385 --name Hand --display-name "Hand Point" --pos-x 0.48 --pos-y 1.52 --rot-x -37.9 --rot-y 81.9
opengil attachment add --input input.gil --output output.gil --prefab-id 1077936385 --object-id 1086324737 --name Hand --display-name "Hand Point"
```

返回重点：

- `result.kind`
- `result.prefabId`
- `result.objectId`
- `result.sceneInstanceCount`
- `result.names`
- `result.changedTopFields`

### `attachment from-decoration`

用途：根据现有 decoration 生成 attachment points。

参数：

- 必填：`--input <path>`
- 必填：`--prefab-id <u64>`
- 可选：`--object-id <u64>`
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil attachment from-decoration --input input.gil --output output.gil --prefab-id 1077936385
opengil attachment from-decoration --input input.gil --output output.gil --prefab-id 1077936385 --object-id 1086324737
```

返回重点：

- `result.kind`
- `result.prefabId`
- `result.objectId`
- `result.sceneInstanceCount`
- `result.names`
- `result.changedTopFields`

### `pixel-art import-decoration`

用途：从 PNG 生成一个像素风 decoration prefab；每个可见像素会写入对应的不透明 signed ARGB 颜色。

参数：

- 必填：`--input <path>`
- 必填：`--png <path>`
- 必填：`--prefab-id <u64>`
- 必填：`--asset-id <u64>`
- 必填：`--pixel-size <number>`
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil pixel-art import-decoration --input input.gil --output output.gil --png pixel.png --prefab-id 1077939001 --asset-id 20001220 --pixel-size 0.25
opengil pixel-art import-decoration --input input.gil --output output.gil --png pixel.png --prefab-id 1077939001 --asset-id 10009001 --tab-id 3 --prefab-only
```

返回重点：

- `result.kind`
- `result.prefabId`
- `result.prefabName`
- `result.previewObjectId`
- `result.targetTab`
- `result.assetId`
- `result.sourcePixelCount`
- `result.decorationCount`
- `result.prefabDecorationIds`
- `result.changedTopFields`

## 2.8 UI Primitive

默认 controller entry id 常量为 `1073741855`。不传 `--controller-entry-id` 时，查询和改单 primitive 的命令默认使用这个值。

### `ui list`

用途：列出某个 controller entry 下的 UI primitive。

参数：

- 必填：`--input <path>`
- 可选：`--controller-entry-id <u64>`，默认 `1073741855`
- 可选：`--report <path>`

示例：

```powershell
opengil ui list --input input.gil
opengil ui list --input input.gil --controller-entry-id 1073741855
```

返回重点：

- `result.kind`
- `result.controllerEntryId`
- `result.hasTop9`
- `result.hasTop46`
- `result.primitiveCount`
- `result.primitives[]`

### `ui set-type`

用途：修改 UI primitive 类型。

参数：

- 必填：`--input <path>`
- 必填：`--primitive-index <size>`
- 必填：`--type-id <u64>`
- 可选：`--controller-entry-id <u64>`，默认 `1073741855`
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil ui set-type --input input.gil --output output.gil --controller-entry-id 1073741855 --primitive-index 0 --type-id 100001
```

返回重点：

- `result.kind`
- `result.primitiveIndex`
- `result.entryId`
- `result.before`
- `result.after`
- `result.changedTopFields`

### `ui set-color`

用途：修改 UI primitive 颜色。

参数：

- 必填：`--input <path>`
- 必填：`--primitive-index <size>`
- 必填：`--color <i64>`
- 可选：`--controller-entry-id <u64>`，默认 `1073741855`
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil ui set-color --input input.gil --output output.gil --controller-entry-id 1073741855 --primitive-index 0 --color -65536
```

返回重点：

- `result.kind`
- `result.before.color`
- `result.after.color`
- `result.changedTopFields`

### `ui set-transform`

用途：修改 UI primitive transform。

参数：

- 必填：`--input <path>`
- 必填：`--primitive-index <size>`
- 可选：`--controller-entry-id <u64>`，默认 `1073741855`
- 可选：UI transform 参数
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil ui set-transform --input input.gil --output output.gil --controller-entry-id 1073741855 --primitive-index 0 --pos-x 10 --pos-y 20 --width 80 --height 80 --scale-x 1 --scale-y 1 --scale-z 1 --rot-z 0
```

返回重点：

- `result.kind`
- `result.before.transform`
- `result.after.transform`
- `result.changedTopFields`

### `ui set-layer`

用途：修改 UI primitive layer。

参数：

- 必填：`--input <path>`
- 必填：`--primitive-index <size>`
- 必填：`--layer <u64>`
- 可选：`--controller-entry-id <u64>`，默认 `1073741855`
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil ui set-layer --input input.gil --output output.gil --controller-entry-id 1073741855 --primitive-index 0 --layer 9
```

返回重点：

- `result.kind`
- `result.before.layer`
- `result.after.layer`
- `result.changedTopFields`

### `ui set-name`

用途：修改 UI primitive 名称。

参数：

- 必填：`--input <path>`
- 必填：`--primitive-index <size>`
- 必填：`--name <string>`
- 可选：`--controller-entry-id <u64>`，默认 `1073741855`
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil ui set-name --input input.gil --output output.gil --controller-entry-id 1073741855 --primitive-index 0 --name ButtonA
```

返回重点：

- `result.kind`
- `result.before.name`
- `result.after.name`
- `result.changedTopFields`

### `ui delete`

用途：删除一个或多个 UI primitive。

参数：

- 必填：`--input <path>`
- 必填：`--primitive-indexes <csv>`
- 可选：`--controller-entry-id <u64>`
- 可选：`--target-controller-entry-id <u64>`
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

`--primitive-indexes` 使用逗号分隔，例如 `0,2,3`。

示例：

```powershell
opengil ui delete --input input.gil --output output.gil --controller-entry-id 1073741855 --primitive-indexes 0,2,3
opengil ui delete --input input.gil --output output.gil --target-controller-entry-id 1073741855 --primitive-indexes 1,4
```

返回重点：

- `result.kind`
- `result.targetControllerEntryId`
- `result.primitiveCount`
- `result.entryIds`
- `result.changedTopFields`

### `ui import-pixel`

用途：把 PNG 导入为 UI primitive 集合。

参数：

- 必填：`--input <path>`
- 必填：`--png <path>`
- 必填：`--pixel-size <number>`
- 可选：`--controller-entry-id <u64>` 或 `--target-controller-entry-id <u64>`，默认 `1073741855`
- 可选：`--output <path>`
- 可选：`--in-place`
- 可选：`--dry-run`
- 可选：`--report <path>`

示例：

```powershell
opengil ui import-pixel --input input.gil --output output.gil --png pixel.png --pixel-size 8
opengil ui import-pixel --input input.gil --output output.gil --png pixel.png --pixel-size 8 --controller-entry-id 1073741840
```

返回重点：

- `result.kind`
- `result.targetControllerEntryId`
- `result.primitiveCount`
- `result.entryIds`
- `result.changedTopFields`

