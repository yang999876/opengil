# openGil 能力说明

这份文档回答：**openGil 现在能做什么，常用命令怎么调用。**
开发规则见 `docs/development.zh.md`。

openGil 面向 `.gil` 文件的安全原子编辑：读取、定位、修改局部结构，并尽量
保留未知字段和未修改区域。机器可读输出默认走 stdout JSON，日志和诊断走
stderr。

当前产物：

```text
build/Release/opengil.exe
build/Release/opengil.cp310-win_amd64.pyd
```

## 使用原则

```powershell
opengil inspect --input input.gil
opengil validate --input input.gil
opengil <写操作> --input input.gil --output output.gil --dry-run
opengil <写操作> --input input.gil --output output.gil
opengil validate --input output.gil
```

`validate` 只检查 `.gil` envelope 和 protobuf wire payload。它不证明 id
唯一、tab 映射完整、scene/preview 镜像完整或没有悬空引用。

写出文件推荐显式传 `--output output.gil`；覆盖原文件必须显式传
`--in-place`。

## 只读命令

```powershell
opengil inspect --input test.gil
opengil validate --input test.gil
opengil diff-summary --before before.gil --after after.gil
opengil list-tabs --input test.gil
opengil list-prefabs --input test.gil
opengil list-prefabs --input test.gil --tab TabName
opengil list-prefab-tabs --input test.gil --prefab-id 1077936130
opengil get-model --input test.gil --prefab-id 1077936130
opengil list-scene-objects --input test.gil
opengil list-preview-objects --input test.gil
opengil list-nodegraphs --input test.gil
opengil custom-vars list --input test.gil
opengil custom-vars list --input test.gil --prefab-id 1077936130
opengil ui list --input test.gil
opengil ui list --input test.gil --controller-entry-id 1073741855
```

`diff-summary` 用于研究 before/after 样本，帮助定位变化的 top-level field。

## 模型和 Prefab

```powershell
opengil set-model --input input.gil --output output.gil --prefab-id 1077936130 --asset-id 20001220
opengil set-empty-model --input input.gil --output output.gil --prefab-id 1077936130
opengil rename-prefab --input input.gil --output output.gil --prefab-id 1077936130 --name "New Name"
opengil delete-prefab --input input.gil --output output.gil --prefab-id 1077936130
opengil clone-prefab --input input.gil --output output.gil --source-prefab-id 1077936385 --tab-id 6 --new-name "Cloned Prefab"
opengil copy-prefab-to-tab --input input.gil --output output.gil --source-prefab-id 1077936385 --tab-id 6
opengil copy-prefab-to-tab --input input.gil --output output.gil --source-prefab-id 1077936385 --tab-id 6 --name "Copied Prefab"
```

Windows shell 下遇到非 ASCII tab 名时，优先使用 `--tab-id`，不要依赖
`--tab`。

`delete-prefab` 会删除 `top4` prefab entry、清理 `top6` 映射、删除
prefab-owned `top27.field1` decoration，并修剪直接引用目标 id 的 `top10`
记录；它不会默认删除 `top8`。

## Object 和 Transform

```powershell
opengil create-scene-object --input input.gil --output output.gil --asset-id 20001220 --object-id 1077938001
opengil create-prefab --input input.gil --output output.gil --asset-id 20001220 --prefab-id 1077938002
opengil create-prefab --input input.gil --output output.gil --asset-id 20001220 --prefab-id 1077938002 --template template.gil
opengil create-scene-prefab-instance --input input.gil --output output.gil --prefab-id 1077938002 --asset-id 20001220 --object-id 1077938003
opengil set-scene-transform --input input.gil --output output.gil --object-id 1086324737 --pos-x 7 --pos-y 8 --pos-z 9
opengil set-preview-transform --input input.gil --output output.gil --object-id 1077936362 --pos-x 1 --pos-y 2 --pos-z 3
```

Transform 写入会替换完整 transform message。未传 position/rotation 默认
`0`，未传 scale 默认 `1`；需要保留轴值时由调用方显式传入原值。

## NodeGraph 和 Projectile

```powershell
opengil attach-nodegraph --input input.gil --output output.gil --prefab-id 1077936130 --nodegraph-id 1073741913
opengil attach-all-nodegraphs --input input.gil --output output.gil --prefab-id 1077936130
opengil set-projectile-motion --input input.gil --output output.gil --prefab-id 1077936385 --angle 80 --speed 20 --gravity 20
opengil set-projectile-motion --input input.gil --output output.gil --prefab-id 1077936385 --x 3.47 --y 19.70 --gravity 20
```

NodeGraph 操作会把已验证 reference 挂到 prefab、scene、preview。Projectile
能力针对已验证的 prefab-space projectile motion component。

## Custom Variables

```powershell
opengil custom-vars add --input input.gil --output output.gil --prefab-id 1077936130 --name openGilVar --type str
opengil custom-vars remove --input input.gil --output output.gil --prefab-id 1077936130 --name openGilVar
opengil custom-vars copy-all --input input.gil --output output.gil --from-prefab-id 1077936130 --to-prefab-id 1077936131
opengil custom-vars sync-tab --input input.gil --output output.gil --source-prefab-id 1077936340 --tab-id 6
```

支持类型：`entity`、`int`、`bool`、`float`、`str`/`string`、`vec`/`vec3`。
当前只写 custom variable 定义，不写运行时值。

## Decoration 和 Attachment

```powershell
opengil decoration add --input input.gil --output output.gil --prefab-id 1077936385 --asset-id 20001220 --name Deco --pos-y 1.9 --scale-x 0.3 --scale-y 0.04 --scale-z 0.3
opengil attachment add --input input.gil --output output.gil --prefab-id 1077936385 --name Hand --display-name "Hand Point" --pos-x 0.48 --pos-y 1.52 --rot-x -37.9 --rot-y 81.9
opengil attachment from-decoration --input input.gil --output output.gil --prefab-id 1077936385
```

`attachment add` 可选传 `--object-id` 只更新一个 scene entry；不传则更新
所有引用该 prefab 的 scene entry。

## UI Primitive

```powershell
opengil ui append --input input.gil --output output.gil --template template.gil --target-controller-entry-id 1073741855 --template-primitive-index 0
opengil ui append-many --input input.gil --output output.gil --template template.gil --target-controller-entry-id 1073741855 --template-primitive-index 0 --count 3
opengil ui retain --input input.gil --output output.gil --controller-entry-id 1073741855 --primitive-indexes 0,2,3
opengil ui set-type --input input.gil --output output.gil --controller-entry-id 1073741855 --primitive-index 0 --type-id 100001
opengil ui set-color --input input.gil --output output.gil --controller-entry-id 1073741855 --primitive-index 0 --color -65536
opengil ui set-transform --input input.gil --output output.gil --controller-entry-id 1073741855 --primitive-index 0 --pos-x 10 --pos-y 20 --width 80 --height 80 --scale-x 1 --scale-y 1 --scale-z 1 --rot-z 0
opengil ui set-layer --input input.gil --output output.gil --controller-entry-id 1073741855 --primitive-index 0 --layer 9
opengil ui set-name --input input.gil --output output.gil --controller-entry-id 1073741855 --primitive-index 0 --name ButtonA
opengil ui copy-transform-from-template --input input.gil --output output.gil --template template.gil --controller-entry-id 1073741855 --primitive-index 0 --template-primitive-index 1
```

`ui import-geometrize` 和 `ui import-pixel` 当前不实现。

## Python Binding

Python binding 适合组合多次修改，CLI JSON 仍是 agent 兼容层。

```python
import opengil

doc = opengil.open("input.gil")
doc.set_model_asset_id(1086324737, 20001220)
doc.validate()
doc.save("output.gil")
```

## 当前不保证

- 不保证完整语义 validate。
- 不写 custom variable 运行时值。
- 不提供完整 GUI。
- 不保证 protobuf generated class 的无损写回。
- 不把 `.gil` 整体转换成巨大 JSON IR 再写回。
- 不保证未验证结构可以猜着改。
